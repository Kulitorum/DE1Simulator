/*
 * DE1 BLE Simulator - Windows GUI
 *
 * Controls a Raspberry Pi running the BLE daemon via TCP.
 * The Pi handles actual BLE peripheral advertising, while this app
 * provides the GUI and simulation logic.
 */

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QGroupBox>
#include <QTimer>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QSpinBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QProgressDialog>
#include <QDateTime>
#include <QScrollBar>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QMainWindow>

// ============================================================================
// Constants and UUIDs
// ============================================================================

namespace DE1 {

// Characteristic short IDs (4 hex digits)
const QString CHAR_VERSION         = "A001";
const QString CHAR_REQUESTED_STATE = "A002";
const QString CHAR_READ_FROM_MMR   = "A005";
const QString CHAR_WRITE_TO_MMR    = "A006";
const QString CHAR_SHOT_SETTINGS   = "A00B";
const QString CHAR_SHOT_SAMPLE     = "A00D";
const QString CHAR_STATE_INFO      = "A00E";
const QString CHAR_HEADER_WRITE    = "A00F";
const QString CHAR_FRAME_WRITE     = "A010";
const QString CHAR_WATER_LEVELS    = "A011";

// Machine States
enum class State : uint8_t {
    Sleep           = 0x00,
    GoingToSleep    = 0x01,
    Idle            = 0x02,
    Busy            = 0x03,
    Espresso        = 0x04,
    Steam           = 0x05,
    HotWater        = 0x06,
    ShortCal        = 0x07,
    SelfTest        = 0x08,
    LongCal         = 0x09,
    Descale         = 0x0A,
    FatalError      = 0x0B,
    Init            = 0x0C,
    NoRequest       = 0x0D,
    SkipToNext      = 0x0E,
    HotWaterRinse   = 0x0F,
    SteamRinse      = 0x10,
    Refill          = 0x11,
    Clean           = 0x12,
    InBootLoader    = 0x13,
    AirPurge        = 0x14,
    SchedIdle       = 0x15
};

// Machine SubStates
enum class SubState : uint8_t {
    Ready           = 0,
    Heating         = 1,
    FinalHeating    = 2,
    Stabilising     = 3,
    Preinfusion     = 4,
    Pouring         = 5,
    Ending          = 6,
    Steaming        = 7,
    DescaleInit     = 8,
    DescaleFillGroup= 9,
    DescaleReturn   = 10,
    DescaleGroup    = 11,
    DescaleSteam    = 12,
    CleanInit       = 13,
    CleanFillGroup  = 14,
    CleanSoak       = 15,
    CleanGroup      = 16,
    RefillState     = 17,
    PausedSteam     = 18,
    UserNotPresent  = 19,
    Puffing         = 20
};

// MMR Addresses
namespace MMR {
    constexpr uint32_t CPU_BOARD_MODEL  = 0x800008;
    constexpr uint32_t MACHINE_MODEL    = 0x80000C;
    constexpr uint32_t FIRMWARE_VERSION = 0x800010;
    constexpr uint32_t FAN_THRESHOLD    = 0x803808;
    constexpr uint32_t GHC_INFO         = 0x80381C;
    constexpr uint32_t GHC_MODE         = 0x803820;
    constexpr uint32_t STEAM_FLOW       = 0x803828;
    constexpr uint32_t SERIAL_NUMBER    = 0x803830;
    constexpr uint32_t HEATER_VOLTAGE   = 0x803834;
    constexpr uint32_t USB_CHARGER      = 0x803854;
    constexpr uint32_t REFILL_KIT       = 0x80385C;

    QString addressName(uint32_t addr) {
        switch (addr) {
            case CPU_BOARD_MODEL: return "CPU_BOARD_MODEL";
            case MACHINE_MODEL: return "MACHINE_MODEL";
            case FIRMWARE_VERSION: return "FIRMWARE_VERSION";
            case FAN_THRESHOLD: return "FAN_THRESHOLD";
            case GHC_INFO: return "GHC_INFO";
            case GHC_MODE: return "GHC_MODE";
            case STEAM_FLOW: return "STEAM_FLOW";
            case SERIAL_NUMBER: return "SERIAL_NUMBER";
            case HEATER_VOLTAGE: return "HEATER_VOLTAGE";
            case USB_CHARGER: return "USB_CHARGER";
            case REFILL_KIT: return "REFILL_KIT";
            default: return QString("0x%1").arg(addr, 6, 16, QChar('0'));
        }
    }
}

// State name helper
QString stateName(State s) {
    switch (s) {
        case State::Sleep: return "Sleep";
        case State::GoingToSleep: return "GoingToSleep";
        case State::Idle: return "Idle";
        case State::Busy: return "Busy";
        case State::Espresso: return "Espresso";
        case State::Steam: return "Steam";
        case State::HotWater: return "HotWater";
        case State::HotWaterRinse: return "Flush";
        case State::Refill: return "Refill";
        case State::Descale: return "Descale";
        case State::Clean: return "Clean";
        default: return QString("State_0x%1").arg(static_cast<int>(s), 2, 16, QChar('0'));
    }
}

QString subStateName(SubState s) {
    switch (s) {
        case SubState::Ready: return "Ready";
        case SubState::Heating: return "Heating";
        case SubState::FinalHeating: return "FinalHeating";
        case SubState::Stabilising: return "Stabilising";
        case SubState::Preinfusion: return "Preinfusion";
        case SubState::Pouring: return "Pouring";
        case SubState::Ending: return "Ending";
        case SubState::Steaming: return "Steaming";
        default: return QString("SubState_%1").arg(static_cast<int>(s));
    }
}

QString charName(const QString& shortId) {
    if (shortId == CHAR_VERSION) return "VERSION";
    if (shortId == CHAR_REQUESTED_STATE) return "REQUESTED_STATE";
    if (shortId == CHAR_READ_FROM_MMR) return "READ_FROM_MMR";
    if (shortId == CHAR_WRITE_TO_MMR) return "WRITE_TO_MMR";
    if (shortId == CHAR_SHOT_SETTINGS) return "SHOT_SETTINGS";
    if (shortId == CHAR_SHOT_SAMPLE) return "SHOT_SAMPLE";
    if (shortId == CHAR_STATE_INFO) return "STATE_INFO";
    if (shortId == CHAR_HEADER_WRITE) return "HEADER_WRITE";
    if (shortId == CHAR_FRAME_WRITE) return "FRAME_WRITE";
    if (shortId == CHAR_WATER_LEVELS) return "WATER_LEVELS";
    return shortId;
}

} // namespace DE1

// ============================================================================
// Binary Codec - Encoding/Decoding helpers
// ============================================================================

namespace BinaryCodec {

inline uint8_t encodeU8P4(double value) {
    return static_cast<uint8_t>(qBound(0.0, value * 16.0, 255.0));
}

inline double decodeU8P4(uint8_t value) {
    return value / 16.0;
}

inline uint16_t encodeU16P12(double value) {
    return static_cast<uint16_t>(qBound(0.0, value * 4096.0, 65535.0));
}

inline uint16_t encodeU16P8(double value) {
    return static_cast<uint16_t>(qBound(0.0, value * 256.0, 65535.0));
}

inline double decodeU16P8(uint16_t value) {
    return value / 256.0;
}

inline double decodeU8P1(uint8_t value) {
    return value / 2.0;
}

inline void encodeU24P16(double value, uint8_t* out) {
    uint32_t encoded = static_cast<uint32_t>(qBound(0.0, value * 65536.0, 16777215.0));
    out[0] = (encoded >> 16) & 0xFF;
    out[1] = (encoded >> 8) & 0xFF;
    out[2] = encoded & 0xFF;
}

inline void encodeShortBE(uint16_t value, uint8_t* out) {
    out[0] = (value >> 8) & 0xFF;
    out[1] = value & 0xFF;
}

inline uint16_t decodeShortBE(const uint8_t* data) {
    return (static_cast<uint16_t>(data[0]) << 8) | data[1];
}

inline void encodeUint32BE(uint32_t value, uint8_t* out) {
    out[0] = (value >> 24) & 0xFF;
    out[1] = (value >> 16) & 0xFF;
    out[2] = (value >> 8) & 0xFF;
    out[3] = value & 0xFF;
}

inline uint32_t decodeAddress(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 16) |
           (static_cast<uint32_t>(data[1]) << 8) |
           static_cast<uint32_t>(data[2]);
}

inline double decodeF8_1_7(uint8_t value) {
    if (value & 0x80) {
        return value & 0x7F;
    } else {
        return value / 10.0;
    }
}

inline uint16_t decodeU10P0(const uint8_t* data) {
    return decodeShortBE(data) & 0x3FF;
}

} // namespace BinaryCodec

// ============================================================================
// Profile Frame Structure
// ============================================================================

struct ProfileFrame {
    int frameIndex = 0;
    uint8_t flags = 0;
    double setVal = 0;
    double temp = 0;
    double duration = 0;
    double triggerVal = 0;
    uint16_t maxVol = 0;
    bool hasExtension = false;
    double limiterValue = 0;
    double limiterRange = 0;

    QString pumpMode() const { return (flags & 0x01) ? "Flow" : "Pressure"; }
    QString sensor() const { return (flags & 0x10) ? "Water" : "Coffee"; }
    QString transition() const { return (flags & 0x20) ? "Smooth" : "Fast"; }
    bool hasExitCondition() const { return flags & 0x02; }
    QString exitType() const {
        if (!(flags & 0x02)) return "None";
        QString compareWhat = (flags & 0x08) ? "Flow" : "Pressure";
        QString compareHow = (flags & 0x04) ? ">" : "<";
        return QString("%1 %2 %3").arg(compareWhat).arg(compareHow).arg(triggerVal, 0, 'f', 1);
    }

    QString toString() const {
        QString s = QString("Frame %1: %2 %3, %4C, %5s")
            .arg(frameIndex)
            .arg(pumpMode())
            .arg(setVal, 0, 'f', 1)
            .arg(temp, 0, 'f', 1)
            .arg(duration, 0, 'f', 1);
        if (maxVol > 0) s += QString(", max %1mL").arg(maxVol);
        if (hasExitCondition()) s += QString(", exit: %1").arg(exitType());
        if (hasExtension) s += QString(" [Limiter: %1/%2]").arg(limiterValue, 0, 'f', 1).arg(limiterRange, 0, 'f', 1);
        return s;
    }
};

struct ProfileHeader {
    uint8_t headerV = 0;
    uint8_t numFrames = 0;
    uint8_t numPreinfuseFrames = 0;
    double minPressure = 0;
    double maxFlow = 0;

    QString toString() const {
        return QString("Header: v%1, %2 frames (%3 preinfuse), minP=%4 bar, maxF=%5 mL/s")
            .arg(headerV).arg(numFrames).arg(numPreinfuseFrames)
            .arg(minPressure, 0, 'f', 1).arg(maxFlow, 0, 'f', 1);
    }
};

// ============================================================================
// Pi Setup Dialog
// ============================================================================

// Embedded daemon source files
static const char* DAEMON_CPP = R"DAEMON(
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
#include <QDebug>

static const QString SERVICE_UUID = "0000A000-0000-1000-8000-00805F9B34FB";
static const QString CCCD_UUID = "00002902-0000-1000-8000-00805f9b34fb";

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

class DE1BleDaemon : public QObject {
    Q_OBJECT
public:
    explicit DE1BleDaemon(quint16 port, QObject *parent = nullptr) : QObject(parent), m_port(port) {
        m_tcpServer = new QTcpServer(this);
        connect(m_tcpServer, &QTcpServer::newConnection, this, &DE1BleDaemon::onTcpConnection);
        m_bleController = QLowEnergyController::createPeripheral(this);
        connect(m_bleController, &QLowEnergyController::connected, this, [this]() {
            qInfo() << "BLE client connected";
            sendToWindows("connected", {{"client", m_bleController->remoteAddress().toString()}});
        });
        connect(m_bleController, &QLowEnergyController::disconnected, this, [this]() {
            qInfo() << "BLE client disconnected";
            sendToWindows("disconnected", {});
            startAdvertising();
        });
        createDE1Service();
    }

    bool start() {
        if (!m_tcpServer->listen(QHostAddress::Any, m_port)) {
            qCritical() << "Failed to start TCP server on port" << m_port;
            return false;
        }
        qInfo() << "TCP server listening on port" << m_port;
        return true;
    }

private:
    void createDE1Service() {
        QLowEnergyServiceData serviceData;
        serviceData.setType(QLowEnergyServiceData::ServiceTypePrimary);
        serviceData.setUuid(QBluetoothUuid(SERVICE_UUID));
        QLowEnergyDescriptorData cccd(QBluetoothUuid(CCCD_UUID), QByteArray(2, 0));

        auto addChar = [&](const QString& name, QLowEnergyCharacteristic::PropertyTypes props, const QByteArray& val, bool notify = false) {
            QLowEnergyCharacteristicData c;
            c.setUuid(QBluetoothUuid(CHAR_UUIDS[name]));
            c.setProperties(props);
            c.setValue(val);
            if (notify) c.addDescriptor(cccd);
            serviceData.addCharacteristic(c);
        };

        addChar("VERSION", QLowEnergyCharacteristic::Read, QByteArray::fromHex("02010000"));
        addChar("REQUESTED_STATE", QLowEnergyCharacteristic::Write, QByteArray(1, 0));
        addChar("READ_FROM_MMR", QLowEnergyCharacteristic::Read | QLowEnergyCharacteristic::Notify | QLowEnergyCharacteristic::Write, QByteArray(20, 0), true);
        addChar("WRITE_TO_MMR", QLowEnergyCharacteristic::Write, QByteArray(20, 0));
        addChar("SHOT_SETTINGS", QLowEnergyCharacteristic::Read | QLowEnergyCharacteristic::Write, QByteArray(9, 0));
        addChar("SHOT_SAMPLE", QLowEnergyCharacteristic::Notify, QByteArray(19, 0), true);
        addChar("STATE_INFO", QLowEnergyCharacteristic::Read | QLowEnergyCharacteristic::Notify, QByteArray::fromHex("0200"), true);
        addChar("HEADER_WRITE", QLowEnergyCharacteristic::Write, QByteArray(5, 0));
        addChar("FRAME_WRITE", QLowEnergyCharacteristic::Write, QByteArray(8, 0));
        addChar("WATER_LEVELS", QLowEnergyCharacteristic::Read | QLowEnergyCharacteristic::Notify, QByteArray::fromHex("4B00"), true);

        m_de1Service = m_bleController->addService(serviceData);
        if (m_de1Service) {
            connect(m_de1Service, &QLowEnergyService::characteristicChanged, this, &DE1BleDaemon::onCharChanged);
        }
    }

    void startAdvertising() {
        QLowEnergyAdvertisingData ad;
        ad.setDiscoverability(QLowEnergyAdvertisingData::DiscoverabilityGeneral);
        ad.setLocalName("DE1-SIM");
        ad.setServices({QBluetoothUuid(SERVICE_UUID)});
        m_bleController->startAdvertising(QLowEnergyAdvertisingParameters(), ad, ad);
        qInfo() << "Advertising as DE1-SIM";
        sendToWindows("advertising", {});
    }

private slots:
    void onTcpConnection() {
        QTcpSocket *s = m_tcpServer->nextPendingConnection();
        if (m_tcpClient) { s->close(); s->deleteLater(); return; }
        m_tcpClient = s;
        qInfo() << "Windows GUI connected";
        connect(s, &QTcpSocket::readyRead, this, &DE1BleDaemon::onTcpData);
        connect(s, &QTcpSocket::disconnected, this, [this]() { m_tcpClient = nullptr; m_bleController->stopAdvertising(); });
        sendToWindows("ready", {{"version", "1.0.0"}});
        startAdvertising();
    }

    void onTcpData() {
        m_buf += m_tcpClient->readAll();
        while (true) {
            int i = m_buf.indexOf('\n');
            if (i < 0) break;
            QByteArray line = m_buf.left(i);
            m_buf = m_buf.mid(i + 1);
            if (line.isEmpty()) continue;
            QJsonObject cmd = QJsonDocument::fromJson(line).object();
            QString action = cmd["cmd"].toString();
            if (action == "notify" || action == "update") {
                QString charId = cmd["char"].toString();
                QByteArray data = QByteArray::fromHex(cmd["data"].toString().toLatin1());
                QString fullUuid = QString("0000%1-0000-1000-8000-00805F9B34FB").arg(charId);
                auto c = m_de1Service->characteristic(QBluetoothUuid(fullUuid));
                if (c.isValid()) m_de1Service->writeCharacteristic(c, data);
            }
        }
    }

    void onCharChanged(const QLowEnergyCharacteristic &c, const QByteArray &v) {
        QString shortUuid = c.uuid().toString().mid(5, 4).toUpper();
        sendToWindows("write", {{"char", shortUuid}, {"data", QString(v.toHex())}});
    }

    void sendToWindows(const QString &event, const QVariantMap &data) {
        if (!m_tcpClient) return;
        QJsonObject obj;
        obj["event"] = event;
        for (auto it = data.begin(); it != data.end(); ++it) obj[it.key()] = QJsonValue::fromVariant(it.value());
        m_tcpClient->write(QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n");
    }

private:
    quint16 m_port;
    QTcpServer *m_tcpServer = nullptr;
    QTcpSocket *m_tcpClient = nullptr;
    QByteArray m_buf;
    QLowEnergyController *m_bleController = nullptr;
    QLowEnergyService *m_de1Service = nullptr;
};

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    DE1BleDaemon daemon(12345);
    if (!daemon.start()) return 1;
    return app.exec();
}

#include "DAEMON_MOC_PLACEHOLDER"
)DAEMON";

static const char* DAEMON_PRO = R"PRO(
QT = core bluetooth network
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = de1-ble-daemon
SOURCES += de1-ble-daemon.cpp
)PRO";

static const char* SETUP_SCRIPT = R"SCRIPT(#!/bin/bash
set -e
echo "=== DE1 BLE Daemon Setup ==="

echo "[1/5] Installing dependencies..."
apt update && apt install -y qt6-base-dev qt6-connectivity-dev bluez

echo "[2/5] Configuring Bluetooth for BLE peripheral mode..."
# Find the bluetooth service file
BLUETOOTH_SERVICE=""
for f in /lib/systemd/system/bluetooth.service /usr/lib/systemd/system/bluetooth.service; do
    [ -f "$f" ] && BLUETOOTH_SERVICE="$f" && break
done
if [ -n "$BLUETOOTH_SERVICE" ]; then
    if ! grep -q -- "--experimental" "$BLUETOOTH_SERVICE"; then
        echo "Enabling BlueZ experimental mode..."
        sed -i '/^ExecStart=.*bluetoothd/ s/$/ --experimental/' "$BLUETOOTH_SERVICE"
    else
        echo "BlueZ experimental mode already enabled"
    fi
else
    echo "WARNING: Bluetooth service file not found"
fi
systemctl daemon-reload
systemctl enable bluetooth
systemctl restart bluetooth
sleep 3
echo "Configuring Bluetooth adapter..."
rfkill unblock bluetooth 2>/dev/null || true
timeout 5 btmgmt power on || echo "Warning: btmgmt power on timed out"
timeout 5 btmgmt le on || echo "Warning: btmgmt le on timed out"
timeout 5 btmgmt advertising on || echo "Warning: btmgmt advertising on timed out"
timeout 3 btmgmt name 'DE1-SIM' || echo "Warning: btmgmt name timed out"
hciconfig hci0 up 2>/dev/null || true
hciconfig hci0 piscan 2>/dev/null || true
sleep 1

echo "[3/5] Stopping existing daemon (if running)..."
systemctl stop de1-ble-daemon 2>/dev/null || true
sleep 1

echo "[4/5] Building daemon..."
cd /tmp/de1-daemon
qmake6 && make -j$(nproc)
cp de1-ble-daemon /usr/local/bin/

echo "[5/5] Configuring systemd service..."
cat > /etc/systemd/system/de1-ble-daemon.service << 'EOF'
[Unit]
Description=DE1 BLE Simulator Daemon
After=bluetooth.target network-online.target
Wants=bluetooth.target
[Service]
Type=simple
TimeoutStartSec=30
ExecStartPre=/bin/sleep 2
ExecStartPre=/bin/bash -c "timeout 5 btmgmt power on || true; timeout 5 btmgmt le on || true; timeout 5 btmgmt advertising on || true; timeout 3 btmgmt name DE1-SIM || true; hciconfig hci0 piscan || true"
ExecStart=/usr/local/bin/de1-ble-daemon
Restart=on-failure
RestartSec=5
[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload && systemctl enable de1-ble-daemon && systemctl start de1-ble-daemon
echo "=== Setup complete! ==="
echo ""
echo "If BLE stops working, check: dmesg | grep -i 'bluetooth.*fail'"
echo "If you see 'Frame reassembly failed', reboot the Pi: sudo reboot"
)SCRIPT";

class PiSetupDialog : public QDialog {
    Q_OBJECT

public:
    PiSetupDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("Setup Raspberry Pi");
        setMinimumWidth(600);
        setMinimumHeight(400);

        auto *layout = new QVBoxLayout(this);

        auto *infoLabel = new QLabel(
            "This wizard installs the DE1 BLE daemon on your Raspberry Pi.\n\n"
            "A terminal window will open where you'll enter your Pi password\n"
            "(you may need to enter it up to 3 times: SSH, SCP, and sudo).\n\n"
            "Prerequisites:\n"
            "  - Raspberry Pi with Raspberry Pi OS (64-bit)\n"
            "  - SSH enabled on the Pi\n"
            "  - Pi connected to your network"
        );
        infoLabel->setWordWrap(true);
        layout->addWidget(infoLabel);

        auto *formLayout = new QFormLayout();
        m_hostEdit = new QLineEdit("DE1-Simulator.local");
        formLayout->addRow("Pi Hostname/IP:", m_hostEdit);
        m_userEdit = new QLineEdit("pi");
        formLayout->addRow("Username:", m_userEdit);
        layout->addLayout(formLayout);

        m_outputLog = new QPlainTextEdit();
        m_outputLog->setReadOnly(true);
        m_outputLog->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        m_outputLog->setFont(QFont("Consolas", 9));
        m_outputLog->setStyleSheet("QPlainTextEdit { background-color: #1e1e1e; color: #d4d4d4; }");
        m_outputLog->setMaximumHeight(150);
        layout->addWidget(m_outputLog);

        auto *buttonLayout = new QHBoxLayout();
        m_installBtn = new QPushButton("Run Setup in Terminal");
        m_installBtn->setMinimumHeight(35);
        m_installBtn->setStyleSheet("QPushButton { font-weight: bold; }");
        m_clearSettingsBtn = new QPushButton("Clear Password on Pi");
        m_clearSettingsBtn->setToolTip("Reset saved Pi hostname/username and clear SSH known hosts");
        m_closeBtn = new QPushButton("Close");
        buttonLayout->addWidget(m_installBtn);
        buttonLayout->addWidget(m_clearSettingsBtn);
        buttonLayout->addStretch();
        buttonLayout->addWidget(m_closeBtn);
        layout->addLayout(buttonLayout);

        connect(m_installBtn, &QPushButton::clicked, this, &PiSetupDialog::onInstall);
        connect(m_clearSettingsBtn, &QPushButton::clicked, this, &PiSetupDialog::onClearSettings);
        connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);

        createTempFiles();
    }

private:
    void log(const QString& msg) {
        m_outputLog->appendPlainText(msg);
        QScrollBar* sb = m_outputLog->verticalScrollBar();
        sb->setValue(sb->maximum());
        QApplication::processEvents();
    }

    void createTempFiles() {
        m_tempDir = QDir::tempPath() + "/de1-daemon";
        QDir().mkpath(m_tempDir);

        QString daemonCode = QString(DAEMON_CPP).replace("DAEMON_MOC_PLACEHOLDER", "de1-ble-daemon.moc");

        QFile cppFile(m_tempDir + "/de1-ble-daemon.cpp");
        if (cppFile.open(QIODevice::WriteOnly)) {
            cppFile.write(daemonCode.toUtf8());
            cppFile.close();
        }

        QFile proFile(m_tempDir + "/de1-ble-daemon.pro");
        if (proFile.open(QIODevice::WriteOnly)) {
            proFile.write(DAEMON_PRO);
            proFile.close();
        }

        QFile shFile(m_tempDir + "/setup.sh");
        if (shFile.open(QIODevice::WriteOnly)) {
            shFile.write(SETUP_SCRIPT);
            shFile.close();
        }

        log("Files prepared in: " + m_tempDir);
        log("Click 'Run Setup in Terminal' to begin.");
    }

private slots:
    void onInstall() {
        QString host = m_hostEdit->text().trimmed();
        QString user = m_userEdit->text().trimmed();

        if (host.isEmpty() || user.isEmpty()) {
            QMessageBox::warning(this, "Missing Info", "Please enter hostname and username.");
            return;
        }

        QString target = QString("%1@%2").arg(user, host);
        QString tempDirWin = m_tempDir;
        tempDirWin.replace("/", "\\");

        // Create a batch script that does everything
        QString batchFile = m_tempDir + "/full_setup.bat";
        QFile batch(batchFile);
        if (batch.open(QIODevice::WriteOnly)) {
            batch.write(QString(
                "@echo off\n"
                "title DE1 BLE Daemon Setup\n"
                "color 0A\n"
                "echo.\n"
                "echo ========================================\n"
                "echo   DE1 BLE Daemon - Raspberry Pi Setup\n"
                "echo ========================================\n"
                "echo.\n"
                "echo Target: %1\n"
                "echo.\n"
                "echo [Step 1/3] Creating directory on Pi...\n"
                "echo.\n"
                "ssh -tt -o StrictHostKeyChecking=accept-new %1 \"mkdir -p /tmp/de1-daemon && echo Directory created OK\"\n"
                "if errorlevel 1 goto :error\n"
                "echo.\n"
                "echo [Step 2/3] Copying files to Pi...\n"
                "echo.\n"
                "scp -o StrictHostKeyChecking=accept-new \"%2\\de1-ble-daemon.cpp\" \"%2\\de1-ble-daemon.pro\" \"%2\\setup.sh\" %1:/tmp/de1-daemon/\n"
                "if errorlevel 1 goto :error\n"
                "echo Files copied.\n"
                "echo.\n"
                "echo [Step 3/3] Running installation script...\n"
                "echo (This takes 3-5 minutes)\n"
                "echo.\n"
                "ssh -tt -o StrictHostKeyChecking=accept-new %1 \"cd /tmp/de1-daemon && sudo bash setup.sh\"\n"
                "echo.\n"
                "if errorlevel 1 (\n"
                "    color 0C\n"
                "    echo ========================================\n"
                "    echo   Installation may have had issues\n"
                "    echo   Check output above for errors\n"
                "    echo ========================================\n"
                ") else (\n"
                "    color 0A\n"
                "    echo ========================================\n"
                "    echo   Installation Complete!\n"
                "    echo ========================================\n"
                "    echo.\n"
                "    echo Now close this window and in DE1 Simulator:\n"
                "    echo   1. Enter '%3' as the Pi address\n"
                "    echo   2. Click Connect\n"
                "    echo   3. Scan with Decenza app for 'DE1-SIM'\n"
                ")\n"
                "echo.\n"
                "goto :end\n"
                ":error\n"
                "color 0C\n"
                "echo.\n"
                "echo ========================================\n"
                "echo   ERROR: Setup failed!\n"
                "echo ========================================\n"
                "echo Check that:\n"
                "echo   - Pi is powered on and connected to network\n"
                "echo   - SSH is enabled on the Pi\n"
                "echo   - Hostname/IP is correct\n"
                "echo   - Password is correct\n"
                ":end\n"
                "echo.\n"
                "echo (Use Ctrl+A, Ctrl+C to copy this log before closing)\n"
                "set /p dummy=Press ENTER to close...\n"
            ).arg(target, tempDirWin, host).toUtf8());
            batch.close();
        }

        log("\nOpening terminal window...");
        log("Enter your Pi password when prompted.");
        log(QString("Host: %1").arg(host));

        // Open the batch file in a new terminal
        QProcess::startDetached("cmd.exe", {"/c", "start", "cmd", "/c", batchFile});

        m_installBtn->setText("Run Again");
    }

    void onClearSettings() {
        QString host = m_hostEdit->text().trimmed();
        if (host.isEmpty()) host = "DE1-Simulator.local";

        // Clear SSH known_hosts entry for this Pi
        QString knownHostsPath = QDir::homePath() + "/.ssh/known_hosts";
        QString msg;

        // Try to remove the host from known_hosts using ssh-keygen
        QProcess sshKeygen;
        sshKeygen.start("ssh-keygen", {"-R", host});
        if (sshKeygen.waitForFinished(5000) && sshKeygen.exitCode() == 0) {
            msg = QString("Cleared SSH known_hosts entry for '%1'.\n\n"
                         "The next SSH connection will ask you to verify\n"
                         "the Pi's fingerprint again.").arg(host);
            log(QString("Cleared SSH known_hosts entry for %1").arg(host));
        } else {
            msg = QString("Could not clear SSH known_hosts for '%1'.\n\n"
                         "You may need to manually edit:\n%2").arg(host, knownHostsPath);
            log(QString("Failed to clear known_hosts: %1").arg(QString(sshKeygen.readAllStandardError())));
        }

        // Also reset saved settings to defaults
        QSettings settings("Decenza", "DE1Simulator");
        settings.remove("pi_host");
        settings.remove("pi_port");
        m_hostEdit->setText("DE1-Simulator.local");
        m_userEdit->setText("pi");

        QMessageBox::information(this, "Password Cleared", msg);
    }

private:
    QLineEdit *m_hostEdit;
    QLineEdit *m_userEdit;
    QPlainTextEdit *m_outputLog;
    QPushButton *m_installBtn;
    QPushButton *m_clearSettingsBtn;
    QPushButton *m_closeBtn;
    QString m_tempDir;
};

// ============================================================================
// DE1 Simulator Main Window
// ============================================================================

class DE1Simulator : public QMainWindow {
    Q_OBJECT

public:
    DE1Simulator(QWidget *parent = nullptr) : QMainWindow(parent) {
        setupUI();
        setupMenuBar();
        loadSettings();

        // TCP socket
        m_socket = new QTcpSocket(this);
        connect(m_socket, &QTcpSocket::connected, this, &DE1Simulator::onConnected);
        connect(m_socket, &QTcpSocket::disconnected, this, &DE1Simulator::onDisconnected);
        connect(m_socket, &QTcpSocket::readyRead, this, &DE1Simulator::onDataReceived);
        connect(m_socket, &QTcpSocket::errorOccurred, this, &DE1Simulator::onSocketError);

        // Simulation timer (5Hz = 200ms)
        m_shotTimer = new QTimer(this);
        m_shotTimer->setInterval(200);
        connect(m_shotTimer, &QTimer::timeout, this, &DE1Simulator::onShotTimerTick);

        // Phase progression timer
        m_phaseTimer = new QTimer(this);
        m_phaseTimer->setSingleShot(true);
        connect(m_phaseTimer, &QTimer::timeout, this, &DE1Simulator::onPhaseTimeout);

        // Water level update timer (every 5 seconds)
        m_waterTimer = new QTimer(this);
        m_waterTimer->setInterval(5000);
        connect(m_waterTimer, &QTimer::timeout, this, &DE1Simulator::sendWaterLevel);

        // Check Pi connection on startup (after window shows)
        QTimer::singleShot(500, this, &DE1Simulator::checkPiOnStartup);
    }

    ~DE1Simulator() {
        saveSettings();
    }

private slots:
    void checkPiOnStartup() {
        QString host = m_hostEdit->text();
        if (host.isEmpty()) {
            showSetupDialog();
            return;
        }

        log("Checking if Pi daemon is running...");
        m_statusLabel->setText("Checking Pi connection...");

        // Try a quick connection
        m_checkSocket = new QTcpSocket(this);
        connect(m_checkSocket, &QTcpSocket::connected, this, [this]() {
            log("Pi daemon is running! Ready to connect.");
            m_statusLabel->setText("Pi daemon found - Click Connect");
            m_statusLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #4CAF50;");
            m_checkSocket->disconnectFromHost();
            m_checkSocket->deleteLater();
            m_checkSocket = nullptr;
        });
        connect(m_checkSocket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            log("Pi daemon not responding - showing setup wizard");
            m_statusLabel->setText("Pi not configured");
            m_statusLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #FF9800;");
            m_checkSocket->deleteLater();
            m_checkSocket = nullptr;

            // Ask user if they want to set up
            auto result = QMessageBox::question(this, "Setup Required",
                "Could not connect to the Pi daemon.\n\n"
                "Would you like to set up the Raspberry Pi now?",
                QMessageBox::Yes | QMessageBox::No);

            if (result == QMessageBox::Yes) {
                showSetupDialog();
            }
        });

        m_checkSocket->connectToHost(host, m_portSpin->value());
    }

private:
    void log(const QString& msg, const QString& category = "INFO") {
        QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        QString formatted = QString("[%1] [%2] %3").arg(timestamp, category, msg);
        m_logView->appendPlainText(formatted);
        QScrollBar* sb = m_logView->verticalScrollBar();
        sb->setValue(sb->maximum());
    }

    void logRx(const QString& msg) { log(msg, "RX"); }
    void logTx(const QString& msg) { log(msg, "TX"); }
    void logPi(const QString& msg) { log(msg, "PI"); }

    void setupMenuBar() {
        auto *menuBar = new QMenuBar(this);
        setMenuBar(menuBar);

        // Tools menu
        auto *toolsMenu = menuBar->addMenu("&Tools");

        auto *setupAction = new QAction("Setup &Raspberry Pi...", this);
        connect(setupAction, &QAction::triggered, this, &DE1Simulator::showSetupDialog);
        toolsMenu->addAction(setupAction);

        // Help menu
        auto *helpMenu = menuBar->addMenu("&Help");

        auto *aboutAction = new QAction("&About", this);
        connect(aboutAction, &QAction::triggered, this, [this]() {
            QMessageBox::about(this, "About DE1 Simulator",
                "DE1 BLE Simulator v1.0\n\n"
                "Simulates a Decent Espresso DE1 machine over BLE.\n"
                "Requires a Raspberry Pi running the BLE daemon.\n\n"
                "https://github.com/your-repo/de1-simulator");
        });
        helpMenu->addAction(aboutAction);
    }

    void setupUI() {
        setWindowTitle("DE1 BLE Simulator");
        setMinimumSize(900, 700);

        auto *centralWidget = new QWidget();
        setCentralWidget(centralWidget);
        auto *mainLayout = new QVBoxLayout(centralWidget);

        // === Connection Section ===
        auto *connGroup = new QGroupBox("Raspberry Pi Connection");
        auto *connLayout = new QHBoxLayout(connGroup);

        connLayout->addWidget(new QLabel("Pi Address:"));
        m_hostEdit = new QLineEdit();
        m_hostEdit->setPlaceholderText("DE1-Simulator.local or IP address");
        m_hostEdit->setMinimumWidth(200);
        connLayout->addWidget(m_hostEdit);

        connLayout->addWidget(new QLabel("Port:"));
        m_portSpin = new QSpinBox();
        m_portSpin->setRange(1, 65535);
        m_portSpin->setValue(12345);
        connLayout->addWidget(m_portSpin);

        m_connectBtn = new QPushButton("Connect");
        m_connectBtn->setMinimumWidth(100);
        connect(m_connectBtn, &QPushButton::clicked, this, &DE1Simulator::onConnectClicked);
        connLayout->addWidget(m_connectBtn);

        connLayout->addStretch();

        mainLayout->addWidget(connGroup);

        // === Status Section ===
        auto *statusGroup = new QGroupBox("Status");
        auto *statusLayout = new QVBoxLayout(statusGroup);

        m_statusLabel = new QLabel("Not connected to Pi");
        m_statusLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #666;");
        statusLayout->addWidget(m_statusLabel);

        auto *stateLayout = new QHBoxLayout();
        stateLayout->addWidget(new QLabel("State:"));
        m_stateLabel = new QLabel("Idle");
        m_stateLabel->setStyleSheet("font-weight: bold; color: #2196F3;");
        stateLayout->addWidget(m_stateLabel);
        stateLayout->addSpacing(20);
        stateLayout->addWidget(new QLabel("SubState:"));
        m_subStateLabel = new QLabel("Ready");
        m_subStateLabel->setStyleSheet("font-weight: bold; color: #4CAF50;");
        stateLayout->addWidget(m_subStateLabel);
        stateLayout->addSpacing(20);
        stateLayout->addWidget(new QLabel("BLE Client:"));
        m_bleClientLabel = new QLabel("None");
        m_bleClientLabel->setStyleSheet("font-weight: bold; color: #666;");
        stateLayout->addWidget(m_bleClientLabel);
        stateLayout->addStretch();
        statusLayout->addLayout(stateLayout);

        mainLayout->addWidget(statusGroup);

        // === GHC Buttons Section ===
        auto *ghcGroup = new QGroupBox("Group Head Controller (GHC) Buttons");
        auto *ghcLayout = new QVBoxLayout(ghcGroup);

        auto *buttonLayout = new QHBoxLayout();

        m_powerBtn = new QPushButton("Power\n(Wake/Sleep)");
        m_espressoBtn = new QPushButton("Espresso");
        m_steamBtn = new QPushButton("Steam");
        m_hotWaterBtn = new QPushButton("Hot Water");
        m_flushBtn = new QPushButton("Flush");

        for (auto *btn : {m_powerBtn, m_espressoBtn, m_steamBtn, m_hotWaterBtn, m_flushBtn}) {
            btn->setMinimumHeight(40);
            btn->setCheckable(true);
            btn->setEnabled(false);  // Disabled until connected
            buttonLayout->addWidget(btn);
        }

        connect(m_powerBtn, &QPushButton::clicked, this, &DE1Simulator::onPowerClicked);
        connect(m_espressoBtn, &QPushButton::clicked, this, &DE1Simulator::onEspressoClicked);
        connect(m_steamBtn, &QPushButton::clicked, this, &DE1Simulator::onSteamClicked);
        connect(m_hotWaterBtn, &QPushButton::clicked, this, &DE1Simulator::onHotWaterClicked);
        connect(m_flushBtn, &QPushButton::clicked, this, &DE1Simulator::onFlushClicked);

        ghcLayout->addLayout(buttonLayout);

        // GHC Status dropdown + Stop button
        auto *ghcStatusLayout = new QHBoxLayout();
        ghcStatusLayout->addWidget(new QLabel("GHC Status:"));
        m_ghcCombo = new QComboBox();
        m_ghcCombo->addItem("0 - Not installed (app CAN start)", 0);
        m_ghcCombo->addItem("1 - Present but unused (app CAN start)", 1);
        m_ghcCombo->addItem("2 - Installed but inactive (app CAN start)", 2);
        m_ghcCombo->addItem("3 - Present and active (app CANNOT start)", 3);
        m_ghcCombo->addItem("4 - Debug mode (app CAN start)", 4);
        m_ghcCombo->setCurrentIndex(3);
        ghcStatusLayout->addWidget(m_ghcCombo);
        ghcStatusLayout->addStretch();

        m_stopBtn = new QPushButton("STOP");
        m_stopBtn->setMinimumWidth(100);
        m_stopBtn->setEnabled(false);
        m_stopBtn->setStyleSheet(
            "QPushButton { background-color: #f44336; color: white; font-weight: bold; }"
            "QPushButton:hover { background-color: #d32f2f; }"
            "QPushButton:disabled { background-color: #ccc; color: #666; }"
        );
        connect(m_stopBtn, &QPushButton::clicked, this, &DE1Simulator::onStopClicked);
        ghcStatusLayout->addWidget(m_stopBtn);

        ghcLayout->addLayout(ghcStatusLayout);

        mainLayout->addWidget(ghcGroup);

        // === Live Values Section ===
        auto *valuesGroup = new QGroupBox("Live Values");
        auto *valuesLayout = new QGridLayout(valuesGroup);

        valuesLayout->addWidget(new QLabel("Pressure:"), 0, 0);
        m_pressureLabel = new QLabel("0.0 bar");
        m_pressureLabel->setStyleSheet("font-weight: bold;");
        valuesLayout->addWidget(m_pressureLabel, 0, 1);

        valuesLayout->addWidget(new QLabel("Flow:"), 0, 2);
        m_flowLabel = new QLabel("0.0 mL/s");
        m_flowLabel->setStyleSheet("font-weight: bold;");
        valuesLayout->addWidget(m_flowLabel, 0, 3);

        valuesLayout->addWidget(new QLabel("Temperature:"), 0, 4);
        m_tempLabel = new QLabel("93.0 C");
        m_tempLabel->setStyleSheet("font-weight: bold;");
        valuesLayout->addWidget(m_tempLabel, 0, 5);

        valuesLayout->addWidget(new QLabel("Shot Timer:"), 1, 0);
        m_timerLabel = new QLabel("0.0 s");
        m_timerLabel->setStyleSheet("font-weight: bold;");
        valuesLayout->addWidget(m_timerLabel, 1, 1);

        valuesLayout->addWidget(new QLabel("Water Level:"), 1, 2);
        m_waterLabel = new QLabel("75 %");
        m_waterLabel->setStyleSheet("font-weight: bold;");
        valuesLayout->addWidget(m_waterLabel, 1, 3);

        valuesLayout->addWidget(new QLabel("Frame:"), 1, 4);
        m_frameLabel = new QLabel("0");
        m_frameLabel->setStyleSheet("font-weight: bold;");
        valuesLayout->addWidget(m_frameLabel, 1, 5);

        mainLayout->addWidget(valuesGroup);

        // === Tabs for Log and Profile ===
        m_tabWidget = new QTabWidget();

        // BLE Log tab with clear button
        auto *logTab = new QWidget();
        auto *logTabLayout = new QVBoxLayout(logTab);
        logTabLayout->setContentsMargins(0, 0, 0, 0);

        m_logView = new QPlainTextEdit();
        m_logView->setReadOnly(true);
        m_logView->setFont(QFont("Consolas", 9));
        m_logView->setStyleSheet("QPlainTextEdit { background-color: #1e1e1e; color: #d4d4d4; }");
        m_logView->setMaximumBlockCount(1000);
        logTabLayout->addWidget(m_logView);

        auto *logBtnLayout = new QHBoxLayout();
        logBtnLayout->addStretch();
        m_clearLogBtn = new QPushButton("Clear Log");
        connect(m_clearLogBtn, &QPushButton::clicked, this, [this]() {
            m_logView->clear();
        });
        logBtnLayout->addWidget(m_clearLogBtn);
        logTabLayout->addLayout(logBtnLayout);

        m_tabWidget->addTab(logTab, "BLE Log");

        m_profileView = new QPlainTextEdit();
        m_profileView->setReadOnly(true);
        m_profileView->setFont(QFont("Consolas", 9));
        m_profileView->setStyleSheet("QPlainTextEdit { background-color: #1e1e1e; color: #d4d4d4; }");
        m_tabWidget->addTab(m_profileView, "Profile");

        mainLayout->addWidget(m_tabWidget, 1);

        // Status bar
        m_statusBar = new QStatusBar();
        setStatusBar(m_statusBar);
        m_statusBar->showMessage("Ready - Connect to Raspberry Pi to start");

        updateStateDisplay();
        updateProfileDisplay();
    }

    void loadSettings() {
        QSettings settings("Decenza", "DE1Simulator");
        m_hostEdit->setText(settings.value("pi_host", "DE1-Simulator.local").toString());
        m_portSpin->setValue(settings.value("pi_port", 12345).toInt());
    }

    void saveSettings() {
        QSettings settings("Decenza", "DE1Simulator");
        settings.setValue("pi_host", m_hostEdit->text());
        settings.setValue("pi_port", m_portSpin->value());
    }

private slots:
    void showSetupDialog() {
        PiSetupDialog dialog(this);
        dialog.exec();
    }

    void onConnectClicked() {
        if (m_socket->state() == QAbstractSocket::ConnectedState) {
            m_socket->disconnectFromHost();
        } else {
            QString host = m_hostEdit->text();
            int port = m_portSpin->value();

            if (host.isEmpty()) {
                QMessageBox::warning(this, "Error", "Please enter the Pi hostname or IP address.");
                return;
            }

            log(QString("Connecting to %1:%2...").arg(host).arg(port));
            m_statusLabel->setText("Connecting...");
            m_statusLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #FF9800;");
            m_connectBtn->setEnabled(false);

            m_socket->connectToHost(host, port);
        }
    }

    void onConnected() {
        log("Connected to Pi daemon");
        m_statusLabel->setText("Connected to Pi - Waiting for BLE...");
        m_statusLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #4CAF50;");
        m_connectBtn->setText("Disconnect");
        m_connectBtn->setEnabled(true);
        m_statusBar->showMessage("Connected to Raspberry Pi");

        // Enable controls
        for (auto *btn : {m_powerBtn, m_espressoBtn, m_steamBtn, m_hotWaterBtn, m_flushBtn, m_stopBtn}) {
            btn->setEnabled(true);
        }

        // Start water level timer
        m_waterTimer->start();
    }

    void onDisconnected() {
        log("Disconnected from Pi");
        m_statusLabel->setText("Disconnected from Pi");
        m_statusLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #666;");
        m_connectBtn->setText("Connect");
        m_connectBtn->setEnabled(true);
        m_bleClientLabel->setText("None");
        m_statusBar->showMessage("Disconnected");

        // Disable controls
        for (auto *btn : {m_powerBtn, m_espressoBtn, m_steamBtn, m_hotWaterBtn, m_flushBtn, m_stopBtn}) {
            btn->setEnabled(false);
        }

        // Stop timers
        m_shotTimer->stop();
        m_phaseTimer->stop();
        m_waterTimer->stop();
    }

    void onSocketError(QAbstractSocket::SocketError error) {
        log(QString("Socket error: %1").arg(m_socket->errorString()), "ERROR");
        m_statusLabel->setText("Connection failed: " + m_socket->errorString());
        m_statusLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #f44336;");
        m_connectBtn->setText("Connect");
        m_connectBtn->setEnabled(true);
    }

    void onDataReceived() {
        m_tcpBuffer += m_socket->readAll();

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
                log("JSON parse error: " + err.errorString(), "ERROR");
                continue;
            }

            handlePiEvent(doc.object());
        }
    }

    void handlePiEvent(const QJsonObject &event) {
        QString type = event["event"].toString();

        if (type == "ready") {
            QString version = event["version"].toString();
            logPi(QString("Pi daemon ready (v%1)").arg(version));
            m_statusLabel->setText("Connected to Pi - Advertising as DE1-SIM");
            m_statusLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #4CAF50;");

            // Send initial state
            sendStateNotification();
            sendWaterLevel();
        }
        else if (type == "advertising") {
            logPi("BLE advertising started");
        }
        else if (type == "connected") {
            QString client = event["client"].toString();
            logPi(QString("BLE client connected: %1").arg(client));
            m_bleClientLabel->setText(client);
            m_bleClientLabel->setStyleSheet("font-weight: bold; color: #4CAF50;");
            m_statusBar->showMessage("BLE client connected: " + client);
        }
        else if (type == "disconnected") {
            logPi("BLE client disconnected");
            m_bleClientLabel->setText("None");
            m_bleClientLabel->setStyleSheet("font-weight: bold; color: #666;");
            m_statusBar->showMessage("BLE client disconnected");
        }
        else if (type == "write") {
            QString charId = event["char"].toString();
            QByteArray data = QByteArray::fromHex(event["data"].toString().toLatin1());
            handleCharacteristicWrite(charId, data);
        }
        else if (type == "read") {
            QString charId = event["char"].toString();
            logRx(QString("CHAR_READ: %1").arg(DE1::charName(charId)));
        }
        else if (type == "error") {
            int code = event["code"].toInt();
            log(QString("Pi BLE error: %1").arg(code), "ERROR");
        }
    }

    void handleCharacteristicWrite(const QString &charId, const QByteArray &value) {
        QString charName = DE1::charName(charId);

        if (charId == DE1::CHAR_REQUESTED_STATE) {
            if (value.size() >= 1) {
                auto requestedState = static_cast<DE1::State>(static_cast<uint8_t>(value[0]));
                logRx(QString("REQUESTED_STATE: %1 (0x%2)")
                    .arg(DE1::stateName(requestedState))
                    .arg(static_cast<int>(requestedState), 2, 16, QChar('0')));
                handleRequestedState(requestedState);
            }
        } else if (charId == DE1::CHAR_READ_FROM_MMR) {
            handleMMRReadRequest(value);
        } else if (charId == DE1::CHAR_WRITE_TO_MMR) {
            handleMMRWrite(value);
        } else if (charId == DE1::CHAR_HEADER_WRITE) {
            handleHeaderWrite(value);
        } else if (charId == DE1::CHAR_FRAME_WRITE) {
            handleFrameWrite(value);
        } else if (charId == DE1::CHAR_SHOT_SETTINGS) {
            handleShotSettings(value);
        } else {
            logRx(QString("%1: %2").arg(charName, value.toHex(' ')));
        }
    }

    void handleRequestedState(DE1::State requested) {
        int ghcStatus = m_ghcCombo->currentData().toInt();
        if (ghcStatus == 3) {
            if (requested != DE1::State::Sleep && requested != DE1::State::Idle) {
                log(QString("GHC active - BLOCKED app request: %1").arg(DE1::stateName(requested)), "WARN");
                return;
            }
        }

        transitionToState(requested, DE1::SubState::Ready);
    }

    void handleMMRReadRequest(const QByteArray &value) {
        if (value.size() < 4) return;

        uint32_t address = BinaryCodec::decodeAddress(reinterpret_cast<const uint8_t*>(value.constData() + 1));
        QString addrName = DE1::MMR::addressName(address);

        logRx(QString("MMR_READ: %1").arg(addrName));

        QByteArray response(8, 0);
        BinaryCodec::encodeUint32BE(address, reinterpret_cast<uint8_t*>(response.data()));

        QString responseVal;
        if (address == DE1::MMR::GHC_INFO) {
            int ghc = m_ghcCombo->currentData().toInt();
            response[4] = static_cast<char>(ghc);
            responseVal = QString::number(ghc);
        } else if (address == DE1::MMR::USB_CHARGER) {
            response[4] = 1;
            responseVal = "1 (on)";
        } else if (address == DE1::MMR::MACHINE_MODEL) {
            response[4] = 2;
            responseVal = "2 (DE1Plus)";
        } else if (address == DE1::MMR::FIRMWARE_VERSION) {
            response[4] = 1; response[5] = 0; response[6] = 0; response[7] = 0;
            responseVal = "1.0.0.0";
        } else {
            responseVal = "0 (unknown addr)";
        }

        sendNotification(DE1::CHAR_READ_FROM_MMR, response);
        logTx(QString("MMR_RESPONSE: %1 = %2").arg(addrName, responseVal));
    }

    void handleMMRWrite(const QByteArray &value) {
        if (value.size() < 8) return;

        uint32_t address = BinaryCodec::decodeAddress(reinterpret_cast<const uint8_t*>(value.constData() + 1));
        uint32_t val = static_cast<uint8_t>(value[4]) |
                       (static_cast<uint8_t>(value[5]) << 8) |
                       (static_cast<uint8_t>(value[6]) << 16) |
                       (static_cast<uint8_t>(value[7]) << 24);

        QString addrName = DE1::MMR::addressName(address);
        logRx(QString("MMR_WRITE: %1 = %2 (0x%3)")
            .arg(addrName)
            .arg(val)
            .arg(val, 8, 16, QChar('0')));
    }

    void handleHeaderWrite(const QByteArray &value) {
        if (value.size() < 5) {
            logRx(QString("HEADER_WRITE: invalid size %1").arg(value.size()));
            return;
        }

        const uint8_t* d = reinterpret_cast<const uint8_t*>(value.constData());
        m_profileHeader.headerV = d[0];
        m_profileHeader.numFrames = d[1];
        m_profileHeader.numPreinfuseFrames = d[2];
        m_profileHeader.minPressure = BinaryCodec::decodeU8P4(d[3]);
        m_profileHeader.maxFlow = BinaryCodec::decodeU8P4(d[4]);

        m_profileFrames.clear();
        m_profileFrames.resize(m_profileHeader.numFrames);

        logRx(QString("HEADER_WRITE: %1").arg(m_profileHeader.toString()));
        updateProfileDisplay();
    }

    void handleFrameWrite(const QByteArray &value) {
        if (value.size() < 8) {
            logRx(QString("FRAME_WRITE: invalid size %1").arg(value.size()));
            return;
        }

        const uint8_t* d = reinterpret_cast<const uint8_t*>(value.constData());
        uint8_t frameIdx = d[0];

        if (frameIdx >= 32) {
            int actualIdx = frameIdx - 32;
            if (actualIdx < m_profileFrames.size()) {
                m_profileFrames[actualIdx].hasExtension = true;
                m_profileFrames[actualIdx].limiterValue = BinaryCodec::decodeU8P4(d[1]);
                m_profileFrames[actualIdx].limiterRange = BinaryCodec::decodeU8P4(d[2]);
                logRx(QString("FRAME_EXT[%1]: limiter=%2, range=%3")
                    .arg(actualIdx)
                    .arg(m_profileFrames[actualIdx].limiterValue, 0, 'f', 1)
                    .arg(m_profileFrames[actualIdx].limiterRange, 0, 'f', 1));
            }
        } else if (frameIdx == m_profileHeader.numFrames) {
            logRx(QString("FRAME_WRITE: Tail frame received (profile complete)"));
        } else if (frameIdx < m_profileFrames.size()) {
            ProfileFrame& frame = m_profileFrames[frameIdx];
            frame.frameIndex = frameIdx;
            frame.flags = d[1];
            frame.setVal = BinaryCodec::decodeU8P4(d[2]);
            frame.temp = BinaryCodec::decodeU8P1(d[3]);
            frame.duration = BinaryCodec::decodeF8_1_7(d[4]);
            frame.triggerVal = BinaryCodec::decodeU8P4(d[5]);
            frame.maxVol = BinaryCodec::decodeU10P0(d + 6);

            logRx(QString("FRAME_WRITE[%1]: %2").arg(frameIdx).arg(frame.toString()));
        } else {
            logRx(QString("FRAME_WRITE: index %1 out of range").arg(frameIdx));
        }

        updateProfileDisplay();
    }

    void handleShotSettings(const QByteArray &value) {
        if (value.size() < 9) {
            logRx(QString("SHOT_SETTINGS: invalid size %1").arg(value.size()));
            return;
        }

        const uint8_t* d = reinterpret_cast<const uint8_t*>(value.constData());
        uint8_t steamTemp = d[1];
        uint8_t steamDuration = d[2];
        uint8_t hotWaterTemp = d[3];
        uint8_t hotWaterVol = d[4];
        uint8_t espressoVol = d[6];
        double groupTemp = BinaryCodec::decodeShortBE(d + 7) / 256.0;

        logRx(QString("SHOT_SETTINGS: steam=%1C/%2s, hotWater=%3C/%4mL, espresso=%5mL, groupTemp=%6C")
            .arg(steamTemp).arg(steamDuration)
            .arg(hotWaterTemp).arg(hotWaterVol)
            .arg(espressoVol)
            .arg(groupTemp, 0, 'f', 1));
    }

    // === Send to Pi ===

    void sendCommand(const QJsonObject &cmd) {
        if (m_socket->state() != QAbstractSocket::ConnectedState) return;

        QByteArray json = QJsonDocument(cmd).toJson(QJsonDocument::Compact) + "\n";
        m_socket->write(json);
        m_socket->flush();
    }

    void sendNotification(const QString &charId, const QByteArray &data) {
        QJsonObject cmd;
        cmd["cmd"] = "notify";
        cmd["char"] = charId;
        cmd["data"] = QString(data.toHex());
        sendCommand(cmd);
    }

    void sendStateNotification() {
        QByteArray data(2, 0);
        data[0] = static_cast<char>(m_currentState);
        data[1] = static_cast<char>(m_currentSubState);

        sendNotification(DE1::CHAR_STATE_INFO, data);
        logTx(QString("STATE_INFO: %1/%2")
            .arg(DE1::stateName(m_currentState))
            .arg(DE1::subStateName(m_currentSubState)));
    }

    void sendWaterLevel() {
        if (m_socket->state() != QAbstractSocket::ConnectedState) return;

        double waterMm = (m_waterLevel / 100.0) * 40.0 - 5.0;
        uint16_t encoded = BinaryCodec::encodeU16P8(waterMm);

        QByteArray data(2, 0);
        BinaryCodec::encodeShortBE(encoded, reinterpret_cast<uint8_t*>(data.data()));

        sendNotification(DE1::CHAR_WATER_LEVELS, data);
    }

    void sendShotSample() {
        if (m_socket->state() != QAbstractSocket::ConnectedState) return;

        QByteArray data(19, 0);
        uint8_t* d = reinterpret_cast<uint8_t*>(data.data());

        uint16_t timerEncoded = static_cast<uint16_t>(m_shotTimer_s * 100);
        BinaryCodec::encodeShortBE(timerEncoded, d);

        uint16_t pressureEncoded = BinaryCodec::encodeU16P12(m_pressure);
        BinaryCodec::encodeShortBE(pressureEncoded, d + 2);

        uint16_t flowEncoded = BinaryCodec::encodeU16P12(m_flow);
        BinaryCodec::encodeShortBE(flowEncoded, d + 4);

        uint16_t mixTempEncoded = BinaryCodec::encodeU16P8(m_temperature);
        BinaryCodec::encodeShortBE(mixTempEncoded, d + 6);

        BinaryCodec::encodeU24P16(m_temperature, d + 8);

        BinaryCodec::encodeShortBE(BinaryCodec::encodeU16P8(m_setTemp), d + 11);
        BinaryCodec::encodeShortBE(BinaryCodec::encodeU16P8(m_setTemp), d + 13);

        d[15] = BinaryCodec::encodeU8P4(m_setPressure);
        d[16] = BinaryCodec::encodeU8P4(m_setFlow);
        d[17] = static_cast<uint8_t>(m_frameNumber);
        d[18] = static_cast<uint8_t>(m_steamTemp);

        sendNotification(DE1::CHAR_SHOT_SAMPLE, data);
    }

    // === Simulation ===

    void onShotTimerTick() {
        m_shotTimer_s += 0.2;
        updateSimulationValues();
        sendShotSample();
        updateValuesDisplay();
    }

    void updateSimulationValues() {
        if (m_currentState == DE1::State::Espresso) {
            if (m_currentSubState == DE1::SubState::Preinfusion) {
                m_pressure = qMin(4.0, m_shotTimer_s * 0.8);
                m_flow = 2.0;
                m_setPressure = 4.0;
                m_setFlow = 2.0;
            } else if (m_currentSubState == DE1::SubState::Pouring) {
                double pouringTime = m_shotTimer_s - 7.0;
                m_pressure = 8.0 + qSin(pouringTime * 0.5) * 1.0;
                m_flow = 2.0 + qSin(pouringTime * 0.3) * 0.5;
                m_setPressure = 9.0;
                m_setFlow = 2.0;
                m_frameNumber = qMin(5, static_cast<int>(pouringTime / 5.0) + 1);
            } else if (m_currentSubState == DE1::SubState::Ending) {
                m_pressure = qMax(0.0, m_pressure - 0.5);
                m_flow = qMax(0.0, m_flow - 0.3);
            }
        } else if (m_currentState == DE1::State::Steam) {
            m_pressure = 1.5;
            m_flow = 0.0;
            m_steamTemp = qMin(150.0, 100.0 + m_shotTimer_s * 2.0);
        } else if (m_currentState == DE1::State::HotWater) {
            m_pressure = 0.5;
            m_flow = 6.0;
        } else if (m_currentState == DE1::State::HotWaterRinse) {
            m_pressure = 1.0;
            m_flow = 8.0;
        }
    }

    void onPhaseTimeout() {
        if (m_currentState == DE1::State::Espresso) {
            if (m_currentSubState == DE1::SubState::Heating) {
                transitionToState(DE1::State::Espresso, DE1::SubState::Preinfusion);
                m_phaseTimer->start(5000);
            } else if (m_currentSubState == DE1::SubState::Preinfusion) {
                transitionToState(DE1::State::Espresso, DE1::SubState::Pouring);
                m_phaseTimer->start(25000);
            } else if (m_currentSubState == DE1::SubState::Pouring) {
                transitionToState(DE1::State::Espresso, DE1::SubState::Ending);
                m_phaseTimer->start(2000);
            } else if (m_currentSubState == DE1::SubState::Ending) {
                stopOperation();
            }
        } else if (m_currentState == DE1::State::Steam) {
            stopOperation();
        } else if (m_currentState == DE1::State::HotWater) {
            stopOperation();
        } else if (m_currentState == DE1::State::HotWaterRinse) {
            stopOperation();
        }
    }

    void transitionToState(DE1::State state, DE1::SubState subState) {
        m_currentState = state;
        m_currentSubState = subState;
        updateStateDisplay();
        sendStateNotification();
    }

    void updateStateDisplay() {
        m_stateLabel->setText(DE1::stateName(m_currentState));
        m_subStateLabel->setText(DE1::subStateName(m_currentSubState));

        m_powerBtn->setChecked(m_currentState == DE1::State::Sleep);
        m_espressoBtn->setChecked(m_currentState == DE1::State::Espresso);
        m_steamBtn->setChecked(m_currentState == DE1::State::Steam);
        m_hotWaterBtn->setChecked(m_currentState == DE1::State::HotWater);
        m_flushBtn->setChecked(m_currentState == DE1::State::HotWaterRinse);
    }

    void updateValuesDisplay() {
        m_pressureLabel->setText(QString("%1 bar").arg(m_pressure, 0, 'f', 1));
        m_flowLabel->setText(QString("%1 mL/s").arg(m_flow, 0, 'f', 1));
        m_tempLabel->setText(QString("%1 C").arg(m_temperature, 0, 'f', 1));
        m_timerLabel->setText(QString("%1 s").arg(m_shotTimer_s, 0, 'f', 1));
        m_waterLabel->setText(QString("%1 %").arg(static_cast<int>(m_waterLevel)));
        m_frameLabel->setText(QString::number(m_frameNumber));
    }

    void updateProfileDisplay() {
        QString text;
        text += "=== CURRENT PROFILE ===\n\n";

        if (m_profileHeader.numFrames == 0) {
            text += "(No profile uploaded yet)\n";
        } else {
            text += m_profileHeader.toString() + "\n\n";

            for (int i = 0; i < m_profileFrames.size(); i++) {
                const auto& frame = m_profileFrames[i];
                if (i < m_profileHeader.numPreinfuseFrames) {
                    text += "[Preinfuse] ";
                } else {
                    text += "[Pour]      ";
                }
                text += frame.toString() + "\n";
            }
        }

        m_profileView->setPlainText(text);
    }

    void startOperation(DE1::State state) {
        if (m_currentState != DE1::State::Idle && m_currentState != DE1::State::Sleep) {
            return;
        }

        m_shotTimer_s = 0.0;
        m_pressure = 0.0;
        m_flow = 0.0;
        m_frameNumber = 0;

        if (state == DE1::State::Espresso) {
            transitionToState(DE1::State::Espresso, DE1::SubState::Heating);
            m_phaseTimer->start(2000);
        } else if (state == DE1::State::Steam) {
            transitionToState(DE1::State::Steam, DE1::SubState::Steaming);
            m_phaseTimer->start(45000);
        } else if (state == DE1::State::HotWater) {
            transitionToState(DE1::State::HotWater, DE1::SubState::Pouring);
            m_phaseTimer->start(30000);
        } else if (state == DE1::State::HotWaterRinse) {
            transitionToState(DE1::State::HotWaterRinse, DE1::SubState::Pouring);
            m_phaseTimer->start(10000);
        }

        m_shotTimer->start();
        updateValuesDisplay();
    }

    void stopOperation() {
        m_shotTimer->stop();
        m_phaseTimer->stop();

        m_pressure = 0.0;
        m_flow = 0.0;
        m_steamTemp = 0.0;
        m_frameNumber = 0;

        transitionToState(DE1::State::Idle, DE1::SubState::Ready);
        updateValuesDisplay();
    }

    void onPowerClicked() {
        if (m_currentState == DE1::State::Sleep) {
            transitionToState(DE1::State::Idle, DE1::SubState::Ready);
        } else {
            stopOperation();
            transitionToState(DE1::State::Sleep, DE1::SubState::Ready);
        }
    }

    void onEspressoClicked() {
        if (m_currentState == DE1::State::Espresso) {
            stopOperation();
        } else {
            startOperation(DE1::State::Espresso);
        }
    }

    void onSteamClicked() {
        if (m_currentState == DE1::State::Steam) {
            stopOperation();
        } else {
            startOperation(DE1::State::Steam);
        }
    }

    void onHotWaterClicked() {
        if (m_currentState == DE1::State::HotWater) {
            stopOperation();
        } else {
            startOperation(DE1::State::HotWater);
        }
    }

    void onFlushClicked() {
        if (m_currentState == DE1::State::HotWaterRinse) {
            stopOperation();
        } else {
            startOperation(DE1::State::HotWaterRinse);
        }
    }

    void onStopClicked() {
        stopOperation();
    }

private:
    // TCP
    QTcpSocket *m_socket = nullptr;
    QTcpSocket *m_checkSocket = nullptr;  // For startup check
    QByteArray m_tcpBuffer;

    // State
    DE1::State m_currentState = DE1::State::Idle;
    DE1::SubState m_currentSubState = DE1::SubState::Ready;

    // Profile data
    ProfileHeader m_profileHeader;
    QVector<ProfileFrame> m_profileFrames;

    // Simulated values
    double m_pressure = 0.0;
    double m_flow = 0.0;
    double m_temperature = 93.0;
    double m_setTemp = 93.0;
    double m_setPressure = 9.0;
    double m_setFlow = 2.0;
    double m_shotTimer_s = 0.0;
    double m_waterLevel = 75.0;
    double m_steamTemp = 0.0;
    int m_frameNumber = 0;

    // Timers
    QTimer *m_shotTimer = nullptr;
    QTimer *m_phaseTimer = nullptr;
    QTimer *m_waterTimer = nullptr;

    // GUI - Connection
    QLineEdit *m_hostEdit = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QPushButton *m_connectBtn = nullptr;

    // GUI - Status
    QLabel *m_statusLabel = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_subStateLabel = nullptr;
    QLabel *m_bleClientLabel = nullptr;
    QStatusBar *m_statusBar = nullptr;

    // GUI - Values
    QLabel *m_pressureLabel = nullptr;
    QLabel *m_flowLabel = nullptr;
    QLabel *m_tempLabel = nullptr;
    QLabel *m_timerLabel = nullptr;
    QLabel *m_waterLabel = nullptr;
    QLabel *m_frameLabel = nullptr;

    // GUI - Buttons
    QPushButton *m_powerBtn = nullptr;
    QPushButton *m_espressoBtn = nullptr;
    QPushButton *m_steamBtn = nullptr;
    QPushButton *m_hotWaterBtn = nullptr;
    QPushButton *m_flushBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;

    QComboBox *m_ghcCombo = nullptr;

    // GUI - Tabs
    QTabWidget *m_tabWidget = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QPlainTextEdit *m_profileView = nullptr;
    QPushButton *m_clearLogBtn = nullptr;
};

// ============================================================================
// Main
// ============================================================================

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("DE1 BLE Simulator");
    app.setOrganizationName("Decenza");

    DE1Simulator simulator;
    simulator.show();

    return app.exec();
}

#include "main.moc"
