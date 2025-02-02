#include "IPCSocket.hpp"
#include "Hyprsunset.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <pwd.h>
#include <thread>

void CIPCSocket::initialize() {
    std::thread([&]() {
        const auto SOCKET = socket(AF_UNIX, SOCK_STREAM, 0);

        if (SOCKET < 0) {
            Debug::log(ERR, "Couldn't start the hyprsunset Socket. (1) IPC will not work.");
            return;
        }

        sockaddr_un       SERVERADDRESS = {.sun_family = AF_UNIX};

        const auto        HISenv     = getenv("HYPRLAND_INSTANCE_SIGNATURE");
        const auto        RUNTIMEdir = getenv("XDG_RUNTIME_DIR");
        const std::string USERID     = std::to_string(getpwuid(getuid())->pw_uid);

        const auto        USERDIR = RUNTIMEdir ? RUNTIMEdir + std::string{"/hypr/"} : "/run/user/" + USERID + "/hypr/";

        std::string       socketPath = HISenv ? USERDIR + std::string(HISenv) + "/.hyprsunset.sock" : USERDIR + ".hyprsunset.sock";

        if (!HISenv)
            mkdir(USERDIR.c_str(), S_IRWXU);

        unlink(socketPath.c_str());

        strcpy(SERVERADDRESS.sun_path, socketPath.c_str());

        bind(SOCKET, (sockaddr*)&SERVERADDRESS, SUN_LEN(&SERVERADDRESS));

        // 10 max queued.
        listen(SOCKET, 10);

        sockaddr_in clientAddress = {};
        socklen_t   clientSize    = sizeof(clientAddress);

        char        readBuffer[1024] = {0};

        Debug::log(LOG, "hyprsunset socket started at {} (fd: {})", socketPath, SOCKET);
        while (1) {
            const auto ACCEPTEDCONNECTION = accept(SOCKET, (sockaddr*)&clientAddress, &clientSize);
            if (ACCEPTEDCONNECTION < 0) {
                Debug::log(ERR, "Couldn't listen on the hyprsunset Socket. (3) IPC will not work.");
                break;
            } else {
                do {
                    Debug::log(LOG, "Accepted incoming socket connection request on fd {}", ACCEPTEDCONNECTION);
                    std::lock_guard<std::mutex> lg(g_pHyprsunset->m_mtTickMutex);

                    auto                        messageSize              = read(ACCEPTEDCONNECTION, readBuffer, 1024);
                    readBuffer[messageSize == 1024 ? 1023 : messageSize] = '\0';
                    if (messageSize == 0)
                        break;
                    std::string request(readBuffer);

                    m_szRequest     = request;
                    m_bRequestReady = true;

                    g_pHyprsunset->tick();
                    while (!m_bReplyReady) { // wait for Hyprsunset to finish processing the request
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    write(ACCEPTEDCONNECTION, m_szReply.c_str(), m_szReply.length());
                    m_bReplyReady = false;
                    m_szReply     = "";

                } while (1);
                Debug::log(LOG, "Closing Accepted Connection");
                close(ACCEPTEDCONNECTION);
            }
        }

        close(SOCKET);
    }).detach();
}

bool CIPCSocket::mainThreadParseRequest() {

    if (!m_bRequestReady)
        return false;

    std::string copy = m_szRequest;

    if (copy == "")
        return false;

    // now we can work on the copy

    Debug::log(LOG, "Received a request: {}", copy);

    // set default reply
    m_szReply       = "ok";
    m_bReplyReady   = true;
    m_bRequestReady = false;

    // config commands
    if (copy.find("gamma") == 0) {
        std::string args = copy.substr(copy.find_first_of(' ') + 1);
        float gamma;
        if (args[0] == '+' || args[0] == '-') {
            gamma = g_pHyprsunset->GAMMA * 100;
            if (args[0] == '-') {
                gamma -= std::stof(args.substr(1));
            } else {
                gamma += std::stof(args.substr(1));
            }
            if (gamma < 0) gamma = 0;
            if (gamma > 100) gamma = 100;
        } else {
            gamma = std::stof(args);
        }

        if (gamma < 0 || gamma > 200) {
            m_szReply = "Invalid gamma value (should be in range 0-200%)";
            return false;
        }

        g_pHyprsunset->GAMMA = gamma / 100;
        return true;
    }

    if (copy.find("temperature") == 0) {
        std::string args = copy.substr(copy.find_first_of(' ') + 1);
        unsigned long long kelvin;
        if (args[0] == '+' || args[0] == '-') {
            kelvin = g_pHyprsunset->KELVIN;
            if (args[0] == '-') {
                kelvin -= std::stoull(args.substr(1));
            } else {
                kelvin += std::stoull(args.substr(1));
            }
            if (kelvin < 1000) kelvin = 1000;
            if (kelvin > 20000) kelvin = 20000;
        } else {
            kelvin = std::stoull(args);
        }

        if (kelvin < 1000 || kelvin > 20000) {
            m_szReply = "Invalid temperature (should be in range 1000-20000)";
            return false;
        }

        g_pHyprsunset->KELVIN = kelvin;
        g_pHyprsunset->identity = false;
        return true;
    }

    if (copy.find("identity") == 0) {
        g_pHyprsunset->identity = true;
        return true;
    }

    m_szReply = "invalid command";
    return false;
}
