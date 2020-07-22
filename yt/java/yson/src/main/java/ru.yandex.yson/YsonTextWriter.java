package ru.yandex.yson;

import java.io.IOException;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.UncheckedIOException;
import java.io.Writer;

import javax.annotation.Nonnull;

/**
 * Writer that generates text yson.
 *
 * All underlying writer exceptions are transformed to UncheckedIOException.
 */
public class YsonTextWriter implements ClosableYsonConsumer {
    private static final char[] digits = "0123456789abcdef".toCharArray();
    private final Writer writer;
    private boolean firstItem = false;

    public YsonTextWriter(@Nonnull StringBuilder builder) {
        this(new StringBuilderWriterAdapter(builder));
    }

    public YsonTextWriter(@Nonnull Writer writer) {
        this.writer = writer;
    }

    public YsonTextWriter(@Nonnull OutputStream output) {
        this(new OutputStreamWriter(output));
    }

    /**
     * Closes underlying reader.
     */
    @Override
    public void close() {
        try {
            writer.close();
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    @Override
    public void onInteger(long value) {
        write(Long.toString(value));
    }

    @Override
    public void onUnsignedInteger(long value) {
        write(Long.toUnsignedString(value));
        write("u");
    }

    @Override
    public void onBoolean(boolean value) {
        write(value ? "%true" : "%false");
    }

    @Override
    public void onDouble(double value) {
        if (Double.isNaN(value)) {
            write("%nan");
        } else {
            write(Double.toString(value));
        }
    }

    @Override
    public void onString(@Nonnull byte[] bytes) {
        write('"');
        appendQuotedBytes(bytes);
        write('"');
    }

    @Override
    public void onEntity() {
        write(YsonTags.ENTITY);
    }

    @Override
    public void onListItem() {
        if (firstItem) {
            firstItem = false;
        } else {
            write(YsonTags.ITEM_SEPARATOR);
        }
    }

    @Override
    public void onBeginList() {
        firstItem = true;
        write(YsonTags.BEGIN_LIST);
    }

    @Override
    public void onEndList() {
        firstItem = false;
        write(YsonTags.END_LIST);
    }

    @Override
    public void onBeginAttributes() {
        firstItem = true;
        write(YsonTags.BEGIN_ATTRIBUTES);
    }

    @Override
    public void onEndAttributes() {
        firstItem = false;
        write(YsonTags.END_ATTRIBUTES);
    }

    @Override
    public void onBeginMap() {
        firstItem = true;
        write(YsonTags.BEGIN_MAP);
    }

    @Override
    public void onEndMap() {
        firstItem = false;
        write(YsonTags.END_MAP);
    }

    @Override
    public void onKeyedItem(@Nonnull byte[] key) {
        if (firstItem) {
            firstItem = false;
        } else {
            write(YsonTags.ITEM_SEPARATOR);
        }
        onString(key);
        write(YsonTags.KEY_VALUE_SEPARATOR);
    }

    private void appendQuotedByte(byte b) {
        switch (b) {
            case '\t':
                write("\\t");
                return;
            case '\n':
                write("\\n");
                return;
            case '\r':
                write("\\r");
                return;
            case '"':
                write("\\\"");
                return;
            case '\\':
                write("\\\\");
                return;
        }
        if (b <= 0x1f || b >= 0x7f) {
            write("\\x");
            write(digits[(b & 255) >>> 4]);
            write(digits[b & 15]);
        } else {
            write(b);
        }
    }

    private void appendQuotedBytes(byte[] bytes) {
        for (byte b : bytes) {
            appendQuotedByte(b);
        }
    }

    void write(int b) {
        try {
            writer.write(b);
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    void write(String s) {
        try {
            writer.write(s);
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    static class StringBuilderWriterAdapter extends Writer {
        private final StringBuilder builder;

        StringBuilderWriterAdapter(StringBuilder builder) {
            this.builder = builder;
        }

        @Override
        public void write(@Nonnull char[] chars, int i, int i1) {
            builder.append(chars, i, i1);
        }

        @Override
        public void flush() {
        }

        @Override
        public void close() {
        }
    }
}

