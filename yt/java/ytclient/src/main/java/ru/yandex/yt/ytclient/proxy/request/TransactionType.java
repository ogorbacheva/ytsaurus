package ru.yandex.yt.ytclient.proxy.request;

import ru.yandex.yt.rpcproxy.ETransactionType;

public enum TransactionType {
    Master(ETransactionType.TT_MASTER, "master"),
    Tablet(ETransactionType.TT_TABLET, "tablet"),
    ;

    private final ETransactionType protoValue;
    private final String stringValue;

    TransactionType(ETransactionType protoValue, String stringValue) {
        this.protoValue = protoValue;
        this.stringValue = stringValue;
    }

    @Override
    public String toString() {
        return stringValue;
    }

    public ETransactionType getProtoValue() {
        return protoValue;
    }
}
