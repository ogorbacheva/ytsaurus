package ru.yandex.yt.ytclient.operations;

import java.io.InputStream;
import java.io.OutputStream;

import ru.yandex.inside.yt.kosher.common.YtFormat;
import ru.yandex.inside.yt.kosher.cypress.YPath;
import ru.yandex.inside.yt.kosher.impl.ytree.serialization.YTreeBinarySerializer;
import ru.yandex.inside.yt.kosher.operations.OperationContext;
import ru.yandex.inside.yt.kosher.operations.Yield;
import ru.yandex.inside.yt.kosher.tables.CloseableIterator;
import ru.yandex.inside.yt.kosher.tables.RetriableIterator;
import ru.yandex.inside.yt.kosher.ytree.YTreeMapNode;
import ru.yandex.inside.yt.kosher.ytree.YTreeNode;
import ru.yandex.inside.yt.kosher.ytree.YTreeStringNode;

/**
 * @author sankear
 */
public class YsonTableEntryType implements YTableEntryType<YTreeMapNode> {

    private final boolean setTableIndex;
    private final boolean setRowIndex;

    public YsonTableEntryType(boolean setTableIndex, boolean setRowIndex) {
        this.setTableIndex = setTableIndex;
        this.setRowIndex = setRowIndex;
    }

    @Override
    public YTreeStringNode format() {
        return YtFormat.YSON_BINARY;
    }

    @Override
    public CloseableIterator<YTreeMapNode> iterator(InputStream input, OperationContext context) {
        context.withSettingIndices(setTableIndex, setRowIndex);
        return YTreeBinarySerializer.iterator(input, context).map(YTreeNode::mapNode);
    }

    @Override
    public RetriableIterator<YTreeMapNode> iterator(YPath path, InputStream input,
                                                    YTreeBinarySerializer.YTreeRetriableIterator.RetryState state) {
        return YTreeBinarySerializer.iterator(path, input, state).uncheckedCast();
    }

    @Override
    @SuppressWarnings("unchecked")
    public Yield<YTreeMapNode> yield(OutputStream[] output) {
        return (Yield) YTreeBinarySerializer.yield(output);
    }

}
