package ru.yandex.yt.ytclient.proxy.request;

import javax.annotation.Nullable;

import tech.ytsaurus.client.request.Format;
import tech.ytsaurus.core.cypress.YPath;

import ru.yandex.inside.yt.kosher.impl.ytree.object.YTreeSerializer;
import ru.yandex.lang.NonNullApi;
import ru.yandex.lang.NonNullFields;
import ru.yandex.yt.ytclient.object.MappedRowSerializer;
import ru.yandex.yt.ytclient.object.WireRowSerializer;
import ru.yandex.yt.ytclient.tables.TableSchema;

@NonNullApi
@NonNullFields
public class WriteTable<T> extends ru.yandex.yt.ytclient.request.WriteTable.BuilderBase<T, WriteTable<T>> {

    public WriteTable(YPath path, WireRowSerializer<T> serializer, @Nullable TableSchema tableSchema) {
        setSerializationContext(new ru.yandex.yt.ytclient.request.WriteTable.SerializationContext<T>(serializer));
        setPath(path).setTableSchema(tableSchema);
    }

    public WriteTable(YPath path, WireRowSerializer<T> serializer) {
        setPath(path);
        setSerializationContext(new ru.yandex.yt.ytclient.request.WriteTable.SerializationContext<T>(serializer));
    }

    public WriteTable(YPath path, YTreeSerializer<T> serializer) {
        setPath(path).setSerializationContext(
                new ru.yandex.yt.ytclient.request.WriteTable.SerializationContext<T>(
                        MappedRowSerializer.forClass(serializer)));
    }

    public WriteTable(YPath path, YTreeSerializer<T> serializer, Format format) {
        setPath(path).setSerializationContext(
                new ru.yandex.yt.ytclient.request.WriteTable.SerializationContext<T>(serializer, format));
    }

    public WriteTable(YPath path, Class<T> objectClazz, @Nullable TableSchema tableSchema) {
       setPath(path).setTableSchema(tableSchema).setSerializationContext(
               new ru.yandex.yt.ytclient.request.WriteTable.SerializationContext<T>(objectClazz));
    }

    public WriteTable(YPath path, Class<T> objectClazz) {
        this(path, objectClazz, null);
    }


    /**
     * @deprecated Use {@link #WriteTable(YPath path, WireRowSerializer<T> serializer)} instead.
     */
    @Deprecated
    public WriteTable(String path, WireRowSerializer<T> serializer) {
        setPath(path).setSerializationContext(
                new ru.yandex.yt.ytclient.request.WriteTable.SerializationContext<T>(serializer));
    }

    /**
     * @deprecated Use {@link #WriteTable(YPath path, YTreeSerializer<T> serializer)} instead.
     */
    @Deprecated
    public WriteTable(String path, YTreeSerializer<T> serializer) {
        this(path, MappedRowSerializer.forClass(serializer));
    }

    @Override
    protected WriteTable<T> self() {
        return this;
    }
}
