package ru.yandex.yt.ytclient.rpc;

import java.util.Arrays;

import com.google.protobuf.ByteString;

import ru.yandex.yt.ytclient.ytree.YTreeNode;
import ru.yandex.yt.TError;
import ru.yandex.yt.ytree.TAttribute;
import ru.yandex.yt.ytree.TAttributes;

public class RpcError extends RuntimeException {
    private final TError error;

    public RpcError(TError error) {
        super(errorMessage(error));
        this.error = error;
        for (TError innerError : error.getInnerErrorsList()) {
            addSuppressed(new RpcError(innerError));
        }
    }

    public TError getError() {
        return error;
    }

    private static String errorMessage(TError error) {
        StringBuilder sb = new StringBuilder();
        sb.append("Error ").append(error.getCode());
        String message = error.getMessage();
        if (message.length() > 0) {
            sb.append(": ").append(message);
        }
        TAttributes attributes = error.getAttributes();
        if (attributes.getAttributesCount() > 0) {
            sb.append(" {");
            for (int index = 0; index < attributes.getAttributesCount(); index++) {
                if (index != 0) {
                    sb.append("; ");
                }
                TAttribute attr = attributes.getAttributes(index);
                sb.append(attr.getKey()).append('=');
                ByteString rawValue = attr.getValue();
                try {
                    sb.append(YTreeNode.parseByteString(rawValue).toString());
                } catch (RuntimeException e) {
                    sb.append("<failed to parse ").append(Arrays.toString(rawValue.toByteArray())).append(">");
                }
            }
            sb.append("}");
        }
        return sb.toString();
    }
}
