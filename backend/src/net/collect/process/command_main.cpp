#include <common/messaging/zmq_command_channel.h>

#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "ems-command <endpoint> <command_id> <submit.json|--query|--verify>\n";
        return 0;
    }
    if (argc != 4) {
        std::cerr << "Use ems-command --help\n";
        return 2;
    }
    ems::messaging::ZmqCommandConfig config;
    config.endpoint = argv[1];
    ems::messaging::ZmqCommandRequest request;
    request.command_id = argv[2];
    const std::string operation = argv[3];
    if (operation == "--query")
        request.payload_json = R"({"op":"query"})";
    else if (operation == "--verify")
        request.payload_json = R"({"op":"verify"})";
    else {
        std::ifstream input(operation, std::ios::binary);
        if (!input) {
            std::cerr << "Cannot open command JSON\n";
            return 2;
        }
        request.payload_json.resize(2049);
        input.read(request.payload_json.data(),
                   static_cast<std::streamsize>(request.payload_json.size()));
        request.payload_json.resize(static_cast<std::size_t>(input.gcount()));
        if (request.payload_json.size() > 2048) {
            std::cerr << "Command JSON exceeds 2048 bytes\n";
            return 2;
        }
    }
    const auto reply = ems::messaging::ZmqCommandClient(config).send(request);
    using Code = ems::messaging::ZmqCommandReplyCode;
    const char* code = reply.code == Code::Accepted     ? "Accepted"
                       : reply.code == Code::Duplicate  ? "Duplicate"
                       : reply.code == Code::Result     ? "Result"
                       : reply.code == Code::Rejected   ? "Rejected"
                       : reply.code == Code::Invalid    ? "Invalid"
                       : reply.code == Code::RetryLater ? "RetryLater"
                                                        : "Unknown";
    std::cout << code << '\n' << reply.reason << '\n';
    return reply.code == Code::Accepted || reply.code == Code::Duplicate ||
                   reply.code == Code::Result
               ? 0
               : 1;
}
