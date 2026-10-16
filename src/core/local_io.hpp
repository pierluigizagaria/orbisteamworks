#pragma once
#include "core/backend.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace osw::core {
bool loopback_address(const std::string &address);
std::vector<std::uint8_t> random_secret();
std::string base64(const std::string &bytes);
bool unbase64(const std::string &text, std::string &bytes, std::size_t limit);
class UdpSocket {
  public:
    UdpSocket(const std::string &address, std::uint16_t port = 0);
    ~UdpSocket();
    UdpSocket(const UdpSocket &) = delete;
    UdpSocket &operator=(const UdpSocket &) = delete;
    std::uint16_t port() const;
    bool receive(std::string &bytes, std::string &address, std::uint16_t &port);
    bool send(const std::string &bytes, const std::string &address, std::uint16_t port);

  private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
class Child {
  public:
    Child();
    ~Child();
    void start(const Config &config);
    bool send(std::string line);
    std::vector<std::string> poll();
    bool alive() const;
    void stop();

  private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
} // namespace osw::core
