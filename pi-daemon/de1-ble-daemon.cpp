/*
 * DE1 BLE Daemon for Raspberry Pi
 *
 * Minimal BLE peripheral that forwards traffic to/from a Windows GUI app.
 * Runs headless on Pi, controlled via TCP from the Windows simulator.
 *
 * Build:
 *   qmake6 && make
 *
 * Run:
 *   sudo ./de1-ble-daemon [port]
 *
 * Default port: 12345
 */

#include <QCoreApplication>
#include <QBluetoothUuid>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QLowEnergyServiceData>
#include <QLowEnergyCharacteristicData>
#include <QLowEnergyDescriptorData>
#include <QLowEnergyAdvertisingData>
#include <QLowEnergyAdvertisingParameters>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QDebug>

// DE1 Service UUID
static const QString SERVICE_UUID = "0000A000-0000-1000-8000-00805F9B34FB";

// Characteristic UUIDs (short form, will be expanded)
static const QMap<QString, QString> CHAR_UUIDS = {
    {"VERSION",         "0000A001-0000-1000-8000-00805F9B34FB"},
    {"REQUESTED_STATE", "0000A002-0000-1000-8000-00805F9B34FB"},
    {"READ_FROM_MMR",   "0000A005-0000-1000-8000-00805F9B34FB"},
    {"WRITE_TO_MMR",    "0000A006-0000-1000-8000-00805F9B34FB"},
    {"SHOT_SETTINGS",   "0000A00B-0000-1000-8000-00805F9B34FB"},
    {"SHOT_SAMPLE",     "0000A00D-0000-1000-8000-00805F9B34FB"},
    {"STATE_INFO",      "0000A00E-0000-1000-8000-00805F9B34FB"},
    {"HEADER_WRITE",    "0000A00F-0000-1000-8000-00805F9B34FB"},
    {"FRAME_WRITE",     "0000A010-0000-1000-8000-00805F9B34FB"},
    {"WATER_LEVELS",    "0000A011-0000-1000-8000-00805F9B34FB"}
};

// CCCD UUID for notifications
static const QString CCCD_UUID = "00002902-0000-1000-8000-00805f9b34fb";

class DE1BleDaemon : public QObject
{
    Q_OBJECT

public:
    explicit DE1BleDaemon(quint16 port, QObject *parent = nullptr)
        : QObject(parent), m_port(port)
    {
        // Setup TCP server
        m_tcpServer = new QTcpServer(this);
        connect(m_tcpServer, &QTcpServer::newConnection, this, &DE1BleDaemon::onTcpConnection);

        // Setup BLE
        setupBluetooth();
    }

    bool start()
    {
        if (!m_tcpServer->listen(QHostAddress::Any, m_port)) {
            qCritical() << "Failed to start TCP server on port" << m_port;
            return false;
        }
        qInfo() << "TCP server listening on port" << m_port;
        qInfo() << "Waiting for Windows GUI connection...";
        return true;
    }

private:
    void setupBluetooth()
    {
        m_bleController = QLowEnergyController::createPeripheral(this);

        connect(m_bleController, &QLowEnergyController::connected, this, [this]() {
            qInfo() << "BLE client connected";
            sendToWindows("connected", {{"client", m_bleController->remoteAddress().toString()}});
        });

        connect(m_bleController, &QLowEnergyController::disconnected, this, [this]() {
            qInfo() << "BLE client disconnected";
            sendToWindows("disconnected", {});
            // Restart advertising
            startAdvertising();
        });

        connect(m_bleController, QOverload<QLowEnergyController::Error>::of(&QLowEnergyController::errorOccurred),
                this, [this](QLowEnergyController::Error error) {
            qWarning() << "BLE error:" << error;
            sendToWindows("error", {{"code", static_cast<int>(error)}});
        });

        // Create service and start advertising immediately
        createDE1Service();

        // Start advertising on startup (don't wait for Windows connection)
        QTimer::singleShot(100, this, &DE1BleDaemon::startAdvertising);
    }

    void createDE1Service()
    {
        QLowEnergyServiceData serviceData;
        serviceData.setType(QLowEnergyServiceData::ServiceTypePrimary);
        serviceData.setUuid(QBluetoothUuid(SERVICE_UUID));

        // VERSION - Read only
        {
            QLowEnergyCharacteristicData charData;
            charData.setUuid(QBluetoothUuid(CHAR_UUIDS["VERSION"]));
            charData.setProperties(QLowEnergyCharacteristic::Read);
            charData.setValue(QByteArray::fromHex("02010000")); // Version 2.1.0.0
            serviceData.addCharacteristic(charData);
        }

        // REQUESTED_STATE - Write
        {
            QLowEnergyCharacteristicData charData;
            charData.setUuid(QBluetoothUuid(CHAR_UUIDS["REQUESTED_STATE"]));
            charData.setProperties(QLowEnergyCharacteristic::Write);
            charData.setValue(QByteArray(1, 0x00));
            serviceData.addCharacteristic(charData);
        }

        // READ_FROM_MMR - Read, Notify
        {
            QLowEnergyCharacteristicData charData;
            charData.setUuid(QBluetoothUuid(CHAR_UUIDS["READ_FROM_MMR"]));
            charData.setProperties(QLowEnergyCharacteristic::Read | QLowEnergyCharacteristic::Notify);
            charData.setValue(QByteArray(8, 0x00));
            QLowEnergyDescriptorData cccd(QBluetoothUuid(CCCD_UUID), QByteArray(2, 0));
            charData.addDescriptor(cccd);
            serviceData.addCharacteristic(charData);
        }

        // WRITE_TO_MMR - Write
        {
            QLowEnergyCharacteristicData charData;
            charData.setUuid(QBluetoothUuid(CHAR_UUIDS["WRITE_TO_MMR"]));
            charData.setProperties(QLowEnergyCharacteristic::Write);
            charData.setValue(QByteArray(8, 0x00));
            serviceData.addCharacteristic(charData);
        }

        // SHOT_SETTINGS - Read, Write
        {
            QLowEnergyCharacteristicData charData;
            charData.setUuid(QBluetoothUuid(CHAR_UUIDS["SHOT_SETTINGS"]));
            charData.setProperties(QLowEnergyCharacteristic::Read | QLowEnergyCharacteristic::Write);
            charData.setValue(QByteArray(8, 0x00));
            serviceData.addCharacteristic(charData);
        }

        // SHOT_SAMPLE - Notify
        {
            QLowEnergyCharacteristicData charData;
            charData.setUuid(QBluetoothUuid(CHAR_UUIDS["SHOT_SAMPLE"]));
            charData.setProperties(QLowEnergyCharacteristic::Notify);
            charData.setValue(QByteArray(19, 0x00));
            QLowEnergyDescriptorData cccd(QBluetoothUuid(CCCD_UUID), QByteArray(2, 0));
            charData.addDescriptor(cccd);
            serviceData.addCharacteristic(charData);
        }

        // STATE_INFO - Read, Notify
        {
            QLowEnergyCharacteristicData charData;
            charData.setUuid(QBluetoothUuid(CHAR_UUIDS["STATE_INFO"]));
            charData.setProperties(QLowEnergyCharacteristic::Read | QLowEnergyCharacteristic::Notify);
            charData.setValue(QByteArray::fromHex("0200")); // Idle, Ready
            QLowEnergyDescriptorData cccd(QBluetoothUuid(CCCD_UUID), QByteArray(2, 0));
            charData.addDescriptor(cccd);
            serviceData.addCharacteristic(charData);
        }

        // HEADER_WRITE - Write
        {
            QLowEnergyCharacteristicData charData;
            charData.setUuid(QBluetoothUuid(CHAR_UUIDS["HEADER_WRITE"]));
            charData.setProperties(QLowEnergyCharacteristic::Write);
            charData.setValue(QByteArray(5, 0x00));
            serviceData.addCharacteristic(charData);
        }

        // FRAME_WRITE - Write
        {
            QLowEnergyCharacteristicData charData;
            charData.setUuid(QBluetoothUuid(CHAR_UUIDS["FRAME_WRITE"]));
            charData.setProperties(QLowEnergyCharacteristic::Write);
            charData.setValue(QByteArray(8, 0x00));
            serviceData.addCharacteristic(charData);
        }

        // WATER_LEVELS - Read, Notify
        {
            QLowEnergyCharacteristicData charData;
            charData.setUuid(QBluetoothUuid(CHAR_UUIDS["WATER_LEVELS"]));
            charData.setProperties(QLowEnergyCharacteristic::Read | QLowEnergyCharacteristic::Notify);
            charData.setValue(QByteArray::fromHex("4B00")); // 75%
            QLowEnergyDescriptorData cccd(QBluetoothUuid(CCCD_UUID), QByteArray(2, 0));
            charData.addDescriptor(cccd);
            serviceData.addCharacteristic(charData);
        }

        m_de1Service = m_bleController->addService(serviceData);

        if (m_de1Service) {
            connect(m_de1Service, &QLowEnergyService::characteristicChanged,
                    this, &DE1BleDaemon::onCharacteristicChanged);
            connect(m_de1Service, &QLowEnergyService::characteristicRead,
                    this, &DE1BleDaemon::onCharacteristicRead);
            qInfo() << "DE1 service created successfully";
        } else {
            qCritical() << "Failed to create DE1 service";
        }
    }

    void startAdvertising()
    {
        if (!m_de1Service) {
            qWarning() << "Cannot advertise - service not created";
            return;
        }

        QLowEnergyAdvertisingData advertisingData;
        advertisingData.setDiscoverability(QLowEnergyAdvertisingData::DiscoverabilityGeneral);
        advertisingData.setLocalName("DE1-SIM");
        // Include service UUID in advertising data so scanners can filter by it
        advertisingData.setServices(QList<QBluetoothUuid>() << QBluetoothUuid(SERVICE_UUID));

        QLowEnergyAdvertisingData scanResponse;
        scanResponse.setLocalName("DE1-SIM");
        // Also include in scan response
        scanResponse.setServices(QList<QBluetoothUuid>() << QBluetoothUuid(SERVICE_UUID));

        m_bleController->startAdvertising(QLowEnergyAdvertisingParameters(),
                                          advertisingData, scanResponse);
        qInfo() << "Started BLE advertising as 'DE1-SIM'";
        sendToWindows("advertising", {});
    }

    void stopAdvertising()
    {
        m_bleController->stopAdvertising();
        qInfo() << "Stopped BLE advertising";
    }

private slots:
    void onTcpConnection()
    {
        QTcpSocket *socket = m_tcpServer->nextPendingConnection();
        if (m_tcpClient) {
            qWarning() << "Rejecting new connection - already have a client";
            socket->close();
            socket->deleteLater();
            return;
        }

        m_tcpClient = socket;
        qInfo() << "Windows GUI connected from" << socket->peerAddress().toString();

        connect(socket, &QTcpSocket::readyRead, this, &DE1BleDaemon::onTcpData);
        connect(socket, &QTcpSocket::disconnected, this, [this]() {
            qInfo() << "Windows GUI disconnected";
            m_tcpClient = nullptr;
            // Keep advertising - don't stop when Windows disconnects
        });

        // Send ready message (advertising already started on daemon startup)
        sendToWindows("ready", {{"version", "1.0.0"}});
    }

    void onTcpData()
    {
        if (!m_tcpClient) return;

        m_tcpBuffer += m_tcpClient->readAll();

        // Process complete JSON messages (newline-delimited)
        while (true) {
            int idx = m_tcpBuffer.indexOf('\n');
            if (idx < 0) break;

            QByteArray line = m_tcpBuffer.left(idx);
            m_tcpBuffer = m_tcpBuffer.mid(idx + 1);

            if (line.isEmpty()) continue;

            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(line, &err);
            if (err.error != QJsonParseError::NoError) {
                qWarning() << "JSON parse error:" << err.errorString();
                continue;
            }

            handleCommand(doc.object());
        }
    }

    void onCharacteristicChanged(const QLowEnergyCharacteristic &c, const QByteArray &value)
    {
        // BLE client wrote to a characteristic
        QString shortUuid = c.uuid().toString().mid(5, 4).toUpper();
        qDebug() << "Characteristic written:" << shortUuid << "->" << value.toHex();

        sendToWindows("write", {
            {"char", shortUuid},
            {"data", QString(value.toHex())}
        });
    }

    void onCharacteristicRead(const QLowEnergyCharacteristic &c, const QByteArray &value)
    {
        // BLE client read a characteristic
        QString shortUuid = c.uuid().toString().mid(5, 4).toUpper();
        qDebug() << "Characteristic read:" << shortUuid;

        sendToWindows("read", {
            {"char", shortUuid}
        });
    }

private:
    void handleCommand(const QJsonObject &cmd)
    {
        QString action = cmd["cmd"].toString();
        qDebug() << "Received command:" << action;

        if (action == "notify") {
            // Send notification to BLE client
            QString charId = cmd["char"].toString();
            QByteArray data = QByteArray::fromHex(cmd["data"].toString().toLatin1());
            sendNotification(charId, data);
        }
        else if (action == "update") {
            // Update characteristic value (for reads)
            QString charId = cmd["char"].toString();
            QByteArray data = QByteArray::fromHex(cmd["data"].toString().toLatin1());
            updateCharacteristic(charId, data);
        }
        else if (action == "start") {
            startAdvertising();
        }
        else if (action == "stop") {
            stopAdvertising();
        }
        else {
            qWarning() << "Unknown command:" << action;
        }
    }

    void sendNotification(const QString &charShortId, const QByteArray &data)
    {
        if (!m_de1Service) return;

        // Find the characteristic by short UUID
        QString fullUuid = QString("0000%1-0000-1000-8000-00805F9B34FB").arg(charShortId);
        QLowEnergyCharacteristic c = m_de1Service->characteristic(QBluetoothUuid(fullUuid));

        if (!c.isValid()) {
            qWarning() << "Characteristic not found:" << charShortId;
            return;
        }

        m_de1Service->writeCharacteristic(c, data);
        qDebug() << "Sent notification on" << charShortId << ":" << data.toHex();
    }

    void updateCharacteristic(const QString &charShortId, const QByteArray &data)
    {
        if (!m_de1Service) return;

        QString fullUuid = QString("0000%1-0000-1000-8000-00805F9B34FB").arg(charShortId);
        QLowEnergyCharacteristic c = m_de1Service->characteristic(QBluetoothUuid(fullUuid));

        if (!c.isValid()) {
            qWarning() << "Characteristic not found:" << charShortId;
            return;
        }

        // For read characteristics, we update the value
        m_de1Service->writeCharacteristic(c, data);
    }

    void sendToWindows(const QString &event, const QVariantMap &data)
    {
        if (!m_tcpClient || m_tcpClient->state() != QAbstractSocket::ConnectedState) {
            return;
        }

        QJsonObject obj;
        obj["event"] = event;
        for (auto it = data.begin(); it != data.end(); ++it) {
            obj[it.key()] = QJsonValue::fromVariant(it.value());
        }

        QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
        m_tcpClient->write(json);
        m_tcpClient->flush();
    }

private:
    quint16 m_port;
    QTcpServer *m_tcpServer = nullptr;
    QTcpSocket *m_tcpClient = nullptr;
    QByteArray m_tcpBuffer;

    QLowEnergyController *m_bleController = nullptr;
    QLowEnergyService *m_de1Service = nullptr;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("DE1 BLE Daemon");
    app.setApplicationVersion("1.0.0");

    quint16 port = 12345;
    if (argc > 1) {
        port = QString(argv[1]).toUShort();
    }

    qInfo() << "DE1 BLE Daemon v1.0.0";
    qInfo() << "---";

    DE1BleDaemon daemon(port);
    if (!daemon.start()) {
        return 1;
    }

    return app.exec();
}

#include "de1-ble-daemon.moc"
