package ru.yandex.yt.ytclient.request;

import ru.yandex.inside.yt.kosher.impl.ytree.object.YTreeRowSerializer;
import ru.yandex.inside.yt.kosher.impl.ytree.object.YTreeSerializer;
import ru.yandex.inside.yt.kosher.impl.ytree.object.serializers.YTreeObjectSerializerFactory;
import ru.yandex.lang.NonNullApi;
import ru.yandex.lang.NonNullFields;
import ru.yandex.yt.ytclient.tables.TableSchema;

@NonNullApi
@NonNullFields
public class ReadTableRequest<T> extends ReadTable<T> {
    public ReadTableRequest(BuilderBase<T, ?> builder) {
        super(builder);
    }

    @Override
    public YTreeRowSerializer<T> createSerializer(Class<T> clazz, TableSchema schema) {
        YTreeSerializer<T> serializer = YTreeObjectSerializerFactory.forClass(clazz, schema);
        if (!(serializer instanceof YTreeRowSerializer)) {
            throw new IllegalArgumentException("Unsupported class: " + clazz);
        }
        return (YTreeRowSerializer<T>) serializer;
    }

    public abstract static class BuilderBase<
            T, TBuilder extends BuilderBase<T, TBuilder>>
            extends ReadTable.BuilderBase<T, TBuilder> {
        @Override
        public ReadTableRequest<T> build() {
            return new ReadTableRequest<>(this);
        }
    }
}
