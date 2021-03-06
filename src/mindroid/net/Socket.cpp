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

#include <mindroid/net/Socket.h>
#include <mindroid/net/SocketAddress.h>
#include <mindroid/net/InetAddress.h>
#include <mindroid/net/Inet4Address.h>
#include <mindroid/net/Inet6Address.h>
#include <mindroid/net/InetSocketAddress.h>
#include <mindroid/net/SocketException.h>
#include <mindroid/io/IOException.h>
#include <mindroid/lang/Class.h>
#include <mindroid/lang/NullPointerException.h>
#include <mindroid/lang/IndexOutOfBoundsException.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <arpa/inet.h>

namespace mindroid {

Socket::Socket(const sp<String>& host, uint16_t port) :
        mLocalAddress(Inet6Address::ANY) {
    sp<InetAddress> inetAddress = InetAddress::getByName(host);
    connect(new InetSocketAddress(inetAddress, port));
}

Socket::~Socket() {
    close();
}

void Socket::close() {
    mIsClosed = true;
    mIsConnected = false;
    mLocalAddress = Inet6Address::ANY;
    if (mFd != -1) {
        ::shutdown(mFd, SHUT_RDWR);
        ::close(mFd);
        mFd = -1;
    }
}

void Socket::connect(const sp<InetSocketAddress>& socketAddress) {
    if (mIsConnected) {
        throw SocketException("Already connected");
    }
    if (mIsClosed) {
        throw SocketException("Already closed");
    }

    sockaddr_storage ss;
    socklen_t saSize = 0;
    std::memset(&ss, 0, sizeof(ss));
    sp<InetAddress> inetAddress = socketAddress->getAddress();
    if (Class<Inet6Address>::isInstance(inetAddress)) {
        mFd = ::socket(AF_INET6, SOCK_STREAM, 0);
        sockaddr_in6& sin6 = reinterpret_cast<sockaddr_in6&>(ss);
        sin6.sin6_family = AF_INET6;
        std::memcpy(&sin6.sin6_addr.s6_addr, inetAddress->getAddress()->c_arr(), 16);
        sin6.sin6_port = htons(socketAddress->getPort());
        saSize = sizeof(sockaddr_in6);
    } else {
        mFd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in& sin = reinterpret_cast<sockaddr_in&>(ss);
        sin.sin_family = AF_INET;
        std::memcpy(&sin.sin_addr.s_addr, inetAddress->getAddress()->c_arr(), 4);
        sin.sin_port = htons(socketAddress->getPort());
        saSize = sizeof(sockaddr_in);
    }

#ifdef __APPLE__
    const int32_t value = 1;
    const int32_t rc = setsockopt(mFd, SOL_SOCKET, SO_NOSIGPIPE, (void*) &value, sizeof(int32_t));
    if (rc < 0) {
        close();
        throw SocketException(String::format("Failed to set socket option SO_NOSIGPIPE (errno=%d)", errno));
    }
#endif

    if (::connect(mFd, (struct sockaddr*) &ss, saSize) == 0) {
        mInetAddress = socketAddress->getAddress();
        mPort = socketAddress->getPort();

        sockaddr_storage ss;
        sockaddr* sa = reinterpret_cast<sockaddr*>(&ss);
        socklen_t saSize = sizeof(ss);
        std::memset(&ss, 0, saSize);
        int32_t rc = ::getsockname(mFd, sa, &saSize);
        if (rc == 0) {
            switch (ss.ss_family) {
            case AF_INET6: {
                const sockaddr_in6& sin6 = *reinterpret_cast<const sockaddr_in6*>(&ss);
                const void* ipAddress = &sin6.sin6_addr.s6_addr;
                size_t ipAddressSize = 16;
                int32_t scope_id = sin6.sin6_scope_id;
                sp<ByteArray> ba = new ByteArray((const uint8_t*) ipAddress, ipAddressSize);
                mLocalAddress = new Inet6Address(ba, nullptr, scope_id);
                mLocalPort = sin6.sin6_port;
                break;
            }
            case AF_INET: {
                const sockaddr_in& sin = *reinterpret_cast<const sockaddr_in*>(&ss);
                const void* ipAddress = &sin.sin_addr.s_addr;
                size_t ipAddressSize = 4;
                sp<ByteArray> ba = new ByteArray((const uint8_t*) ipAddress, ipAddressSize);
                mLocalAddress = new Inet4Address(ba, nullptr);
                mLocalPort = sin.sin_port;
                break;
            }
            default:
                break;
            }
        }
        mIsBound = true;
        mIsConnected = true;
    } else {
        close();
        throw SocketException(String::format("Failed to connect to %s (errno=%d)", inetAddress->toString()->c_str(), errno));
    }
}

sp<InputStream> Socket::getInputStream() {
    if (!mIsConnected) {
        throw IOException("Socket is not connected");
    }
    return new SocketInputStream(this);
}

sp<OutputStream> Socket::getOutputStream() {
    if (!mIsConnected) {
        throw IOException("Socket is not connected");
    }
    return new SocketOutputStream(this);
}

sp<InetAddress> Socket::getLocalAddress() const {
    return mLocalAddress;
}

int32_t Socket::getLocalPort() const {
    if (!isBound()) {
        return -1;
    }
    return mLocalPort;
}

sp<InetAddress> Socket::getInetAddress() const {
    if (!isConnected()) {
        return nullptr;
    }
    return mInetAddress;
}

int32_t Socket::getPort() const {
    if (!isConnected()) {
        return 0;
    }
    return mPort;
}

sp<InetSocketAddress> Socket::getLocalSocketAddress() const {
    if (!isBound()) {
        return nullptr;
    }
    return new InetSocketAddress(getLocalAddress(), getLocalPort());
}

sp<InetSocketAddress> Socket::getRemoteSocketAddress() const {
    if (!isConnected()) {
        return nullptr;
    }
    return new InetSocketAddress(getInetAddress(), getPort());
}

size_t Socket::SocketInputStream::available() {
    return 0;
}

int32_t Socket::SocketInputStream::read() {
    if (mFd == -1) {
        throw IOException("Socket already closed");
    }

    uint8_t data;
    ssize_t rc = ::recv(mFd, reinterpret_cast<char*>(&data), sizeof(data), 0);
    if (rc < 0) {
        throw IOException(String::format("Failed to read from socket (errno=%d)", errno));
    } else {
        return rc != 0 ? rc : -1;
    }
}

ssize_t Socket::SocketInputStream::read(const sp<ByteArray>& buffer, size_t offset, size_t count) {
    if (buffer == nullptr) {
        throw NullPointerException();
    }
    if ((offset + count) > buffer->size()) {
        throw IndexOutOfBoundsException();
    }
    if (mFd == -1) {
        throw IOException("Socket already closed");
    }

    ssize_t rc = ::recv(mFd, reinterpret_cast<char*>(buffer->c_arr() + offset), count, 0);
    if (rc < 0) {
        throw IOException(String::format("Failed to read from socket (errno=%d)", errno));
    } else {
        return rc != 0 ? rc : -1;
    }
}

void Socket::SocketOutputStream::write(int32_t b) {
    if (mFd == -1) {
        throw IOException("Socket already closed");
    }

    uint8_t data = (uint8_t) b;
#ifndef __APPLE__
    ssize_t rc = ::send(mFd, reinterpret_cast<const char*>(&data), sizeof(data), MSG_NOSIGNAL);
#else
    ssize_t rc = ::send(mFd, reinterpret_cast<const char*>(&data), sizeof(data), 0);
#endif
    if (rc < 0 || ((size_t) rc) != sizeof(data)) {
        throw IOException(String::format("Failed to write to socket (errno=%d)", (rc < 0) ? errno : -1));
    }
}

void Socket::SocketOutputStream::write(const sp<ByteArray>& buffer, size_t offset, size_t count) {
    if (buffer == nullptr) {
        throw NullPointerException();
    }
    if ((offset + count) > buffer->size()) {
        throw IndexOutOfBoundsException();
    }
    if (mFd == -1) {
        throw IOException("Socket already closed");
    }

    if (count == 0) {
        return;
    }
#ifndef __APPLE__
    ssize_t rc = ::send(mFd, reinterpret_cast<const char*>(buffer->c_arr() + offset), count, MSG_NOSIGNAL);
#else
    ssize_t rc = ::send(mFd, reinterpret_cast<const char*>(buffer->c_arr() + offset), count, 0);
#endif
    if (rc < 0 || ((size_t) rc) != count) {
        throw IOException(String::format("Failed to write to socket (errno=%d)", (rc < 0) ? errno : -1));
    }
}

} /* namespace mindroid */
