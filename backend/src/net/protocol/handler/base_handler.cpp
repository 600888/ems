/**
 * @file base_handler.cpp
 * @brief 规约处理器基类非虚方法实现
 */
#include "base_handler.h"

namespace ems {
namespace net {
namespace device {

void ProtocolHandler::addTxMessage(const std::vector<uint8_t>& data, const std::string& description) {
    if (messageCapture_) messageCapture_->addTx(data, description);
}

void ProtocolHandler::addRxMessage(const std::vector<uint8_t>& data, const std::string& description) {
    if (messageCapture_) messageCapture_->addRx(data, description);
}

void ProtocolHandler::addTx(const std::vector<uint8_t>& data, const std::string& description) {
    if (messageCapture_) messageCapture_->addTx(data, description);
}

void ProtocolHandler::addRx(const std::vector<uint8_t>& data, const std::string& description) {
    if (messageCapture_) messageCapture_->addRx(data, description);
}

std::optional<ValueType> ProtocolHandler::getConfig(const std::string& key) const {
    auto it = config_.find(key);
    if (it != config_.end()) return it->second;
    return std::nullopt;
}

std::vector<CapturedMessage> ProtocolHandler::getCapturedMessages(int limit) const {
    if (messageCapture_) return messageCapture_->getMessages(limit);
    return {};
}

void ProtocolHandler::clearCapturedMessages() {
    if (messageCapture_) messageCapture_->clear();
}

AvgTimeStats ProtocolHandler::getAvgTime() const {
    if (messageCapture_) return messageCapture_->getAvgTime();
    return {};
}

int ProtocolHandler::read_batch(const std::vector<std::string>&, std::vector<PointValue>&) {
    return -1;  // 子类实现
}

} // namespace device
} // namespace net
} // namespace ems
