/*
 * Copyright (C) 2012 Daniel Himmelein
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <mindroid/net/DatagramSocket.h>
#include <mindroid/net/DatagramPacket.h>
#include <mindroid/net/Inet4Address.h>
#include <mindroid/net/Inet6Address.h>
#include <mindroid/net/InetSocketAddress.h>
#include <mindroid/net/SocketException.h>
#include <mindroid/lang/NullPointerException.h>
#include <mindroid/lang/Class.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <arpa/inet.h>

namespace mindroid {

DatagramSocket::DatagramSocket() :
        DatagramSocket(0) {
}

DatagramSocket::DatagramSocket(uint16_t port) :
        DatagramSocket(port, Inet6Address::ANY) {
}

DatagramSocket::DatagramSocket(uint16_t port, const sp<InetAddress>& inetAddress) {
    bind(port, inetAddress);
}

DatagramSocket::~DatagramSocket() {
    close();
}

void DatagramSocket::close() {
    mIsClosed = true;
    mIsBound = false;
    if (mFd != -1) {
        ::shutdown(mFd, SHUT_RDWR);
        ::close(mFd);
        mFd = -1;
    }
}

void DatagramSocket::bind(const sp<InetSocketAddress>& localAddress) {
    if (localAddress == nullptr) {
        throw NullPointerException();
    }
    if (localAddress->getAddress() == nullptr) {
        throw SocketException(String::format("Host is unresolved: %s", localAddress->getHostName()->c_str()));
    }
    bind((uint16_t) localAddress->getPort(), localAddress->getAddress());
}

void DatagramSocket::bind(uint16_t port, const sp<InetAddress>& localAddress) {
    if (mIsBound) {
        throw SocketException("Socket is already bound");
    }

    sp<InetAddress> address;
    if (localAddress == nullptr) {
        address = Inet6Address::ANY;
    } else {
        address = localAddress;
    }

    sockaddr_storage ss;
    socklen_t saSize = 0;
    std::memset(&ss, 0, sizeof(ss));
    if (Class<Inet6Address>::isInstance(address)) {
        mFd = ::socket(AF_INET6, SOCK_DGRAM, 0);
        int32_t value = 0;
        ::setsockopt(mFd, SOL_SOCKET, IPV6_V6ONLY, &value, sizeof(value));
        sockaddr_in6& sin6 = reinterpret_cast<sockaddr_in6&>(ss);
        sin6.sin6_family = AF_INET6;
        std::memcpy(&sin6.sin6_addr.s6_addr, address->getAddress()->c_arr(), 16);
        sin6.sin6_port = htons(port);
        saSize = sizeof(sockaddr_in6);
    } else {
        mFd = ::socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in& sin = reinterpret_cast<sockaddr_in&>(ss);
        sin.sin_family = AF_INET;
        std::memcpy(&sin.sin_addr.s_addr, address->getAddress()->c_arr(), 4);
        sin.sin_port = htons(port);
        saSize = sizeof(sockaddr_in);
    }
    if (::bind(mFd, (struct sockaddr*) &ss, saSize) == 0) {
        mPort = port;
        mIsBound = true;
    } else {
        ::close(mFd);
        mFd = -1;
        throw SocketException(String::format("Failed to bind socket: errno=%d", errno));
    }
}

void DatagramSocket::receive(const sp<DatagramPacket>& datagramPacket) {
    struct sockaddr_storage sender;
    socklen_t socklen = sizeof(sockaddr_storage);
    ssize_t rc = ::recvfrom(mFd, reinterpret_cast<char*>(datagramPacket->getData()->c_arr() + datagramPacket->getOffset()), datagramPacket->getLength(), 0, (struct sockaddr*) &sender, &socklen);
    if (rc > 0) {
        datagramPacket->setLength((size_t) rc);

        switch (sender.ss_family) {
        case AF_INET6: {
            const sockaddr_in6& sin6 = *reinterpret_cast<const sockaddr_in6*>(&sender);
            const void* ipAddress = &sin6.sin6_addr.s6_addr;
            size_t ipAddressSize = 16;
            int32_t scope_id = sin6.sin6_scope_id;
            sp<ByteArray> ba = new ByteArray((const uint8_t*) ipAddress, ipAddressSize);
            sp<InetAddress> inetAddress = new Inet6Address(ba, nullptr, scope_id);
            datagramPacket->setAddress(inetAddress);
            break;
        }
        case AF_INET: {
            const sockaddr_in& sin = *reinterpret_cast<const sockaddr_in*>(&sender);
            const void* ipAddress = &sin.sin_addr.s_addr;
            size_t ipAddressSize = 4;
            sp<ByteArray> ba = new ByteArray((const uint8_t*) ipAddress, ipAddressSize);
            sp<InetAddress> inetAddress = new Inet4Address(ba, nullptr);
            datagramPacket->setAddress(inetAddress);
            break;
        }
        default:
            break;
        }
    } else {
        throw IOException();
    }
}

void DatagramSocket::send(const sp<DatagramPacket>& datagramPacket) {
    sp<InetAddress> inetAddress = datagramPacket->getAddress();
    if (inetAddress == nullptr) {
        throw NullPointerException("Destination address is null");
    }

    sockaddr_storage ss;
    socklen_t saSize = 0;
    std::memset(&ss, 0, sizeof(ss));
    if (Class<Inet6Address>::isInstance(inetAddress)) {
        sockaddr_in6& sin6 = reinterpret_cast<sockaddr_in6&>(ss);
        sin6.sin6_family = AF_INET6;
        std::memcpy(&sin6.sin6_addr.s6_addr, inetAddress->getAddress()->c_arr(), 16);
        sin6.sin6_port = htons(datagramPacket->getPort());
        saSize = sizeof(sockaddr_in6);
    } else {
        sockaddr_in& sin = reinterpret_cast<sockaddr_in&>(ss);
        sin.sin_family = AF_INET;
        std::memcpy(&sin.sin_addr.s_addr, inetAddress->getAddress()->c_arr(), 4);
        sin.sin_port = htons(datagramPacket->getPort());
        saSize = sizeof(sockaddr_in);
    }

    ssize_t rc = ::sendto(mFd, reinterpret_cast<const char*>(datagramPacket->getData()->c_arr() + datagramPacket->getOffset()),
            datagramPacket->getLength(), 0, (struct sockaddr*) &ss, saSize);
    if (rc < 0 || ((size_t) rc) != datagramPacket->getLength()) {
        throw IOException();
    }
}

} /* namespace mindroid */
