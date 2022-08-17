package ru.yandex.yt.ytclient.operations;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.Closeable;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.net.URI;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.NoSuchElementException;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.Supplier;
import java.util.jar.Attributes;
import java.util.jar.JarEntry;
import java.util.jar.JarFile;
import java.util.jar.JarOutputStream;
import java.util.jar.Manifest;
import java.util.stream.Collectors;
import java.util.zip.ZipEntry;

import javax.annotation.Nonnull;
import javax.annotation.Nullable;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import ru.yandex.inside.yt.kosher.common.GUID;
import ru.yandex.inside.yt.kosher.cypress.CypressNodeType;
import ru.yandex.inside.yt.kosher.cypress.YPath;
import ru.yandex.inside.yt.kosher.impl.ytree.builder.YTree;
import ru.yandex.inside.yt.kosher.ytree.YTreeNode;
import ru.yandex.lang.NonNullApi;
import ru.yandex.lang.NonNullFields;
import ru.yandex.yt.ytclient.proxy.FileWriter;
import ru.yandex.yt.ytclient.proxy.TransactionalClient;
import ru.yandex.yt.ytclient.proxy.request.CreateNode;
import ru.yandex.yt.ytclient.proxy.request.GetFileFromCache;
import ru.yandex.yt.ytclient.proxy.request.GetFileFromCacheResult;
import ru.yandex.yt.ytclient.proxy.request.ListNode;
import ru.yandex.yt.ytclient.proxy.request.MoveNode;
import ru.yandex.yt.ytclient.proxy.request.ObjectType;
import ru.yandex.yt.ytclient.proxy.request.PutFileToCache;
import ru.yandex.yt.ytclient.proxy.request.RemoveNode;
import ru.yandex.yt.ytclient.proxy.request.WriteFile;

public class SingleUploadFromClassPathJarsProcessor implements JarsProcessor {

    private static final Logger LOG = LoggerFactory.getLogger(SingleUploadFromClassPathJarsProcessor.class);

    private static final String NATIVE_FILE_EXTENSION = "so";
    protected static final int DEFAULT_JARS_REPLICATION_FACTOR = 10;

    private final YPath jarsDir;
    protected final YPath cacheDir;
    private final int fileCacheReplicationFactor;

    private final Duration uploadTimeout;
    private final boolean uploadNativeLibraries;
    private final Map<String, YPath> uploadedJars = new HashMap<>();
    private final Map<String, Supplier<InputStream>> uploadMap = new HashMap<>();
    private volatile Instant lastUploadTime;

    private static final char[] DIGITS = {
            '0', '1', '2', '3', '4', '5', '6', '7',
            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };

    public SingleUploadFromClassPathJarsProcessor(YPath jarsDir, @Nullable YPath cacheDir) {
        this(jarsDir, cacheDir, false, Duration.ofMinutes(10), DEFAULT_JARS_REPLICATION_FACTOR);
    }

    public SingleUploadFromClassPathJarsProcessor(YPath jarsDir, @Nullable YPath cacheDir,
                                                  boolean uploadNativeLibraries) {
        this(jarsDir, cacheDir, uploadNativeLibraries, Duration.ofMinutes(10), DEFAULT_JARS_REPLICATION_FACTOR);
    }

    public SingleUploadFromClassPathJarsProcessor(
            YPath jarsDir,
            @Nullable YPath cacheDir,
            boolean uploadNativeLibraries,
            Duration uploadTimeout) {
        this(jarsDir, cacheDir, uploadNativeLibraries, uploadTimeout, DEFAULT_JARS_REPLICATION_FACTOR);
    }

    public SingleUploadFromClassPathJarsProcessor(
            YPath jarsDir,
            @Nullable YPath cacheDir,
            boolean uploadNativeLibraries,
            Duration uploadTimeout,
            @Nullable Integer fileCacheReplicationFactor) {
        this.jarsDir = jarsDir;
        this.cacheDir = cacheDir;
        this.uploadTimeout = uploadTimeout;
        this.uploadNativeLibraries = uploadNativeLibraries;
        this.fileCacheReplicationFactor = fileCacheReplicationFactor != null
                ? fileCacheReplicationFactor
                : DEFAULT_JARS_REPLICATION_FACTOR;
    }

    @Override
    public Set<YPath> uploadJars(TransactionalClient yt, MapperOrReducer<?, ?> mapperOrReducer, boolean isLocalMode) {
        synchronized (this) {
            try {
                uploadIfNeeded(yt, isLocalMode);
            } catch (Exception e) {
                throw new RuntimeException(e);
            }

            return Collections.unmodifiableSet(new HashSet<>(uploadedJars.values()));
        }
    }

    protected void withJar(File jarFile, Consumer<File> consumer) {
        consumer.accept(jarFile);
    }

    private boolean isUsingFileCache() {
        return cacheDir != null;
    }

    private void uploadIfNeeded(TransactionalClient yt, boolean isLocalMode) throws IOException {
        uploadMap.clear();

        yt.createNode(new CreateNode(jarsDir, CypressNodeType.MAP)
                .setRecursive(true)
                .setIgnoreExisting(true)
        ).join();

        if (!isUsingFileCache() && lastUploadTime != null && Instant.now()
                .isBefore(lastUploadTime.plus(uploadTimeout))) {
            return;
        }

        uploadedJars.clear();

        collectJars(yt);
        if (uploadNativeLibraries) {
            collectNativeLibs();
        }
        doUpload(yt, isLocalMode);
    }

    protected void writeFile(TransactionalClient yt, YPath path, InputStream data) {
        FileWriter writer = yt.writeFile(new WriteFile(path.toString()).setComputeMd5(true)).join();
        try {
            byte[] bytes = new byte[0x10000];
            for (; ; ) {
                int count = data.read(bytes);
                if (count < 0) {
                    break;
                }

                writer.write(bytes, 0, count);
            }
            writer.close().join();
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }

    private YPath onFileChecked(TransactionalClient yt, @Nullable YPath path, String originalName, String md5,
                                Supplier<InputStream> fileContent) throws IOException {
        YPath res = path;

        if (res == null) {
            YPath jarPath = jarsDir.child(calculateYPath(fileContent, originalName));
            YPath tmpPath = jarsDir.child(GUID.create().toString());

            LOG.info(String.format("Uploading %s as %s", originalName, jarPath));

            writeFile(yt, tmpPath, fileContent.get());

            res = yt.putFileToCache(new PutFileToCache(tmpPath, cacheDir, md5)).join().getPath();
            yt.removeNode(new RemoveNode(tmpPath).setRecursive(false).setForce(true)).join();
        }

        res = res
                .plusAdditionalAttribute("file_name", originalName)
                .plusAdditionalAttribute("md5", md5)
                .plusAdditionalAttribute("cache", cacheDir.toTree());

        return res;
    }

    @NonNullFields
    @NonNullApi
    protected static class CacheUploadTask {
        final CompletableFuture<Optional<YPath>> cacheCheckResult;
        final String md5;
        final Map.Entry<String, Supplier<InputStream>> entry;
        @Nullable Future<YPath> result;

        public CacheUploadTask(CompletableFuture<Optional<YPath>> cacheCheckResult,
                               String md5, Map.Entry<String, Supplier<InputStream>> entry) {
            this.cacheCheckResult = cacheCheckResult;
            this.md5 = md5;
            this.entry = entry;
        }
    }

    protected List<CacheUploadTask> checkInCache(TransactionalClient yt, Map<String, Supplier<InputStream>> uploadMap) {
        List<CacheUploadTask> tasks = new ArrayList<>();
        for (Map.Entry<String, Supplier<InputStream>> entry : uploadMap.entrySet()) {
            String md5 = calculateMd5(entry.getValue().get());
            CompletableFuture<Optional<YPath>> future =
                    yt.getFileFromCache(new GetFileFromCache(cacheDir, md5))
                            .thenApply(GetFileFromCacheResult::getPath);
            tasks.add(new CacheUploadTask(future, md5, entry));
        }
        return tasks;
    }

    private void checkInCacheAndUpload(TransactionalClient yt, Map<String, Supplier<InputStream>> uploadMap) {
        List<CacheUploadTask> tasks = checkInCache(yt, uploadMap);

        int threadsCount = Math.min(uploadMap.size(), 5);
        ExecutorService executor = Executors.newFixedThreadPool(threadsCount);

        try {
            for (CacheUploadTask task : tasks) {
                task.result = executor.submit(() -> {
                    try {
                        Optional<YPath> path = task.cacheCheckResult.get();
                        return onFileChecked(yt, path.orElse(null), task.entry.getKey(), task.md5,
                                task.entry.getValue());
                    } catch (Exception ex) {
                        throw new RuntimeException(ex);
                    }
                });
            }

            for (CacheUploadTask task : tasks) {
                try {
                    uploadedJars.put(task.entry.getKey(), task.result.get());
                } catch (Exception ex) {
                    throw new RuntimeException(ex);
                }
            }
        } finally {
            executor.shutdown();
        }
    }

    static class UploadTask {
        Future<YPath> result;
        String fileName;

        UploadTask(String fileName) {
            this.result = new CompletableFuture<>();
            this.fileName = fileName;
        }
    }

    private void uploadToTemp(
            TransactionalClient yt,
            Map<String, Supplier<InputStream>> uploadMap,
            boolean isLocalMode
    ) {
        int threadsCount = Math.min(uploadMap.size(), 5);
        ExecutorService executor = Executors.newFixedThreadPool(threadsCount);
        try {
            ArrayList<UploadTask> uploadTasks = new ArrayList<>();
            for (Map.Entry<String, Supplier<InputStream>> entry : uploadMap.entrySet()) {
                String fileName = entry.getKey();
                UploadTask task = new UploadTask(fileName);
                task.result = executor.submit(() -> maybeUpload(yt, entry.getValue(), fileName, isLocalMode));
                uploadTasks.add(task);
            }

            for (UploadTask uploadTask : uploadTasks) {
                try {
                    YPath path = uploadTask.result.get();
                    uploadedJars.put(uploadTask.fileName, path);
                } catch (Exception ex) {
                    throw new RuntimeException(ex);
                }
            }
        } finally {
            executor.shutdown();
        }
    }

    private void doUpload(TransactionalClient yt, boolean isLocalMode) throws IOException {
        if (uploadMap.isEmpty()) {
            return;
        }

        if (isUsingFileCache()) {
            checkInCacheAndUpload(yt, uploadMap);
        } else {
            uploadToTemp(yt, uploadMap, isLocalMode);
        }

        lastUploadTime = Instant.now();
    }

    private static List<File> getFilesList(File file) {
        File[] files = file.listFiles();
        if (files == null) {
            return Arrays.asList();
        }
        return Arrays.asList(files);
    }

    private static Iterator<File> walk(File dir) {
        return append(
                Arrays.asList(dir).iterator(),
                flatMap(getFilesList(dir).iterator(), SingleUploadFromClassPathJarsProcessor::walk));
    }

    private static <T> Iterator<T> flatMap(Iterator<T> source, Function<T, ? extends Iterator<T>> f) {
        class FlatMappedIterator implements Iterator<T> {
            @Nullable private Iterator<T> cur = null;

            @Override
            public boolean hasNext() {
                while ((cur == null || !cur.hasNext()) && source.hasNext()) {
                    cur = f.apply(source.next());
                }
                if (cur == null) {
                    return false;
                }
                return cur.hasNext();
            }

            @Override
            public T next() {
                if (hasNext()) {
                    return cur.next();
                }
                throw new NoSuchElementException("next on empty iterator");
            }

        }
        return new FlatMappedIterator();
    }

    private static <T> Iterator<T> append(Iterator<T> a, Iterator<T> b) {
        return new Iterator<T>() {
            public boolean hasNext() {
                return a.hasNext() || b.hasNext();
            }

            public T next() {
                if (a.hasNext()) {
                    return a.next();
                } else {
                    return b.next();
                }
            }
        };
    }

    private File getParentFile(File file) {
        File parent = file.getParentFile();
        if (parent != null) {
            return parent;
        } else {
            String path = file.getPath();
            if (!path.contains("/") && !path.contains(".")) {
                return new File(".");
            }
            throw new RuntimeException(this + " has no parent");
        }
    }

    private String toHex(byte[] data) {
        StringBuilder result = new StringBuilder();
        for (byte b : data) {
            result.append(DIGITS[(0xF0 & b) >>> 4]);
            result.append(DIGITS[0x0F & b]);
        }
        return result.toString();
    }

    protected String calculateMd5(InputStream stream) {
        try {
            MessageDigest md = MessageDigest.getInstance("MD5");
            byte[] bytes = new byte[0x1000];
            for (; ; ) {
                int len = stream.read(bytes);
                if (len < 0) {
                    break;
                }
                md.update(bytes, 0, len);
            }

            return toHex(md.digest());
        } catch (NoSuchAlgorithmException | IOException ex) {
            throw new RuntimeException(ex);
        }
    }

    private void collectJars(TransactionalClient yt) {
        yt.createNode(new CreateNode(jarsDir, CypressNodeType.MAP)
                .setRecursive(true)
                .setIgnoreExisting(true)
        ).join();
        if (isUsingFileCache()) {
            yt.createNode(new CreateNode(cacheDir, CypressNodeType.MAP)
                    .setRecursive(true)
                    .setIgnoreExisting(true)
            ).join();
        }

        Set<String> existsJars = yt.listNode(new ListNode(jarsDir)).join().asList().stream()
                .map(YTreeNode::stringValue)
                .collect(Collectors.toSet());

        if (!isUsingFileCache() && !uploadedJars.isEmpty()) {
            if (uploadedJars.values().stream().allMatch(p -> existsJars.contains(p.name()))) {
                return;
            }
        }

        Set<String> classPathParts = getClassPathParts();
        for (String classPathPart : classPathParts) {
            File classPathItem = new File(classPathPart);
            if ("jar".equalsIgnoreCase(getFileExtension(classPathItem))) {
                if (!classPathItem.exists()) {
                    throw new IllegalStateException("Can't find " + classPathItem);
                }
                if (classPathItem.isFile()) {
                    withJar(
                            classPathItem,
                            jar -> collectFile(() -> {
                                try {
                                    return new FileInputStream(jar);
                                } catch (FileNotFoundException ex) {
                                    throw new RuntimeException(ex);
                                }
                            }, classPathItem.getName(), existsJars));
                }
            } else if (classPathItem.isDirectory()) {
                byte[] jarBytes = getClassPathDirJarBytes(classPathItem);
                collectFile(() -> new ByteArrayInputStream(jarBytes), classPathItem.getName() + ".jar", existsJars);
            }
        }
    }

    private static String getFileExtension(@Nonnull File file) {
        return Optional.of(file.getName())
                .filter(f -> f.contains("."))
                .map(f -> f.substring(file.getName().lastIndexOf(".") + 1))
                .orElse("");
    }

    private void collectNativeLibs() {
        String libPath = System.getProperty("java.library.path");
        if (libPath == null) {
            throw new IllegalStateException("System property 'java.library.path' is null");
        }
        LOG.info("Searching native libs in " + libPath);

        String[] classPathParts = libPath.split(File.pathSeparator);
        for (String classPathPart : classPathParts) {
            File classPathItem = new File(classPathPart);
            if (classPathItem.isDirectory()) {
                Iterator<File> iter = walk(classPathItem);
                while (iter.hasNext()) {
                    File elm = iter.next();
                    if (elm.isFile() && !Files.isSymbolicLink(elm.toPath())  && NATIVE_FILE_EXTENSION
                            .equalsIgnoreCase(getFileExtension(elm))) {
                        withJar(elm, dll -> collectFile(
                                () -> {
                                    try {
                                        return new FileInputStream(dll);
                                    } catch (FileNotFoundException ex) {
                                        throw new RuntimeException(ex);
                                    }
                                },
                                dll.getName(),
                                Collections.emptySet()));
                    }
                }
            }
        }
    }

    /**
     * @return set of classpath files (usually *.jar)
     */
    private Set<String> getClassPathParts() {
        Set<String> classPathParts = new HashSet<>();
        String classPath = System.getProperty("java.class.path");
        if (classPath == null) {
            throw new IllegalStateException("System property 'java.class.path' is null");
        }
        LOG.info("Searching libs in " + classPath);

        String[] classPathPartsRaw = classPath.split(File.pathSeparator);

        Attributes.Name classPathKey = new Attributes.Name("Class-Path");
        for (String classPathPart : classPathPartsRaw) {
            classPathParts.add(classPathPart);

            try {
                File jarFile = new File(classPathPart);
                Manifest m = new JarFile(classPathPart).getManifest();
                if (m != null) {
                    Attributes a = m.getMainAttributes();
                    if (a.containsKey(classPathKey)) {
                        String[] fileList = a.getValue(classPathKey).split(" ");

                        for (String entity : fileList) {
                            try {
                                File jarFileChild;
                                if (entity.startsWith("file:")) {
                                    jarFileChild = new File(new URI(entity));
                                } else {
                                    jarFileChild = new File(entity);
                                }
                                if (!jarFileChild.isAbsolute()) {
                                    jarFileChild = new File(getParentFile(jarFile), entity);
                                }

                                if (jarFileChild.exists()) {
                                    classPathParts.add(jarFileChild.getPath());
                                }
                            } catch (Throwable e) {
                                LOG.info(String.format("cannot open : %s ", entity), e);
                            }
                        }
                    }
                }
            } catch (IOException ignored) {
            }
        }
        return classPathParts;
    }

    private String calculateYPath(Supplier<InputStream> fileContent, String originalName) {
        String md5 = calculateMd5(fileContent.get());
        String[] parts = originalName.split("\\.");
        String ext = parts.length < 2 ? "" : parts[parts.length - 1];

        return md5 + "." + ext;
    }

    private void collectFile(
            Supplier<InputStream> fileContent, String originalName, Set<String> existsFiles) {
        String fileName = calculateYPath(fileContent, originalName);
        boolean exists = existsFiles.contains(fileName);
        if (isUsingFileCache() || !exists) {
            if (!uploadMap.containsKey(originalName)) {
                uploadMap.put(originalName, fileContent);
            } else if (originalName.endsWith(".jar")) {
                String baseName = originalName.split("\\.")[0];
                uploadMap.put(baseName + "-" + calculateMd5(fileContent.get()) + ".jar", fileContent);
            }
        }
        if (!isUsingFileCache() && exists) {
            uploadedJars.put(originalName, jarsDir.child(fileName));
        }
    }

    private YPath maybeUpload(
            TransactionalClient yt,
            Supplier<InputStream> fileContent,
            String originalName,
            boolean isLocalMode) throws IOException {
        String md5 = calculateMd5(fileContent.get());
        YPath jarPath;
        if (originalName.endsWith(NATIVE_FILE_EXTENSION)) {
            // TODO: do we really need this?
            YPath dllDir = jarsDir.child(md5);
            yt.createNode(new CreateNode(dllDir, CypressNodeType.MAP)
                    .setRecursive(true)
                    .setIgnoreExisting(true)
            ).join();
            jarPath = dllDir.child(originalName);
        } else {
            jarPath = jarsDir.child(calculateYPath(fileContent, originalName));
        }

        YPath tmpPath = jarsDir.child(GUID.create().toString());

        LOG.info(String.format("Uploading %s as %s", originalName, jarPath));

        int actualFileCacheReplicationFactor = isLocalMode ? 1 : fileCacheReplicationFactor;

        yt.createNode(new CreateNode(tmpPath, ObjectType.File)
                .addAttribute("replication_factor", YTree.integerNode(actualFileCacheReplicationFactor))
                .setIgnoreExisting(true)
        ).join();

        writeFile(yt, tmpPath, fileContent.get());

        yt.moveNode(new MoveNode(tmpPath, jarPath).setPreserveAccount(true).setRecursive(true).setForce(true)).join();

        return jarPath.plusAdditionalAttribute("file_name", originalName);
    }

    private static byte[] getClassPathDirJarBytes(File dir) {
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            JarOutputStream jar = new JarOutputStream(bytes) {
                @Override
                public void putNextEntry(ZipEntry ze) throws IOException {
// makes resulting jar md5 predictable to allow jar hashing at yt side
// https://stackoverflow.com/questions/26525936/java-creating-two-identical-zip-files-if-content-are-the-same
                    ze.setTime(-1);
                    super.putNextEntry(ze);
                }
            };
            Iterator<File> iter = walk(dir);
            while (iter.hasNext()) {
                File elm = iter.next();
                String name = elm.getAbsolutePath().substring(dir.getAbsolutePath().length());
                if (name != null && name.length() > 0) {
                    JarEntry entry = new JarEntry(name.substring(1).replace("\\", "/"));
                    jar.putNextEntry(entry);
                    if (elm.isFile()) {
                        FileInputStream file = new FileInputStream(elm);
                        try {
                            writeFileToJarStream(file, jar);
                        } finally {
                            closeQuietly(file);
                        }
                    }
                }
            }
            jar.close();
            return bytes.toByteArray();
        } catch (IOException ex) {
            throw new RuntimeException(ex);
        }
    }

    private static void closeQuietly(@Nullable Closeable closeable) {
        if (closeable != null) {
            try {
                closeable.close();
            } catch (Throwable ex) {
                if (ex instanceof VirtualMachineError) {
                    // outer instanceof for performance (to not make 3 comparisons for most cases)
                    if (ex instanceof OutOfMemoryError || ex instanceof InternalError || ex instanceof UnknownError) {
                        throw (VirtualMachineError) ex;
                    }
                }
            }
        }
    }

    private static void writeFileToJarStream(FileInputStream file, JarOutputStream jar) {
        try {
            byte[] bytes = new byte[0x10000];
            for (; ; ) {
                int count = file.read(bytes);
                if (count < 0) {
                    return;
                }

                jar.write(bytes, 0, count);
            }
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }
}
