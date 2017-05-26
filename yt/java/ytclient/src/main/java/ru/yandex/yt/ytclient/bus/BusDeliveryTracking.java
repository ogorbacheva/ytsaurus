package ru.yandex.yt.ytclient.bus;

/**
 * Необходимый уровень подтверждения о доставке сообщения
 */
public enum BusDeliveryTracking {
    /**
     * Не следить за отправной пакета
     */
    NONE,

    /**
     * Уведомлять о статусе записи пакета в соединение
     */
    SENT,

    /**
     * Уведомлять о статусе доставки после получения подтверждения от другой стороны
     */
    FULL
}
