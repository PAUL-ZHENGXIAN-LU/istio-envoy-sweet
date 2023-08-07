#include "source/extensions/filters/network/dubbo_proxy/conn_manager.h"

#include <cstdint>

#include "envoy/common/exception.h"

#include "source/common/common/fmt.h"
#include "source/extensions/filters/network/dubbo_proxy/app_exception.h"
#include "source/extensions/filters/network/dubbo_proxy/dubbo_hessian2_serializer_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/dubbo_protocol_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/heartbeat_response.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

constexpr uint32_t BufferLimit = UINT32_MAX;

ConnectionManager::ConnectionManager(Config& config, Random::RandomGenerator& random_generator,
                                     TimeSource& time_system)
    : config_(config), time_system_(time_system), stats_(config_.stats()),
      random_generator_(random_generator), protocol_(config.createProtocol()),
      decoder_(std::make_unique<RequestDecoder>(*protocol_, *this)) {}

Network::FilterStatus ConnectionManager::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "dubbo: read {} bytes", data.length());
  request_buffer_.move(data);
  dispatch();

  if (end_stream) {
    ENVOY_CONN_LOG(trace, "downstream closed", read_callbacks_->connection());

    ENVOY_LOG(debug, "dubbo: end data processing");
    resetAllMessages(false);
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }

  return Network::FilterStatus::StopIteration;
}

Network::FilterStatus ConnectionManager::onNewConnection() {
  return Network::FilterStatus::Continue;
}

void ConnectionManager::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  read_callbacks_->connection().addConnectionCallbacks(*this);
  read_callbacks_->connection().enableHalfClose(true);
  read_callbacks_->connection().setBufferLimits(BufferLimit);
}

void ConnectionManager::onEvent(Network::ConnectionEvent event) {
  resetAllMessages(event == Network::ConnectionEvent::LocalClose);
}

void ConnectionManager::onAboveWriteBufferHighWatermark() {
  ENVOY_CONN_LOG(debug, "onAboveWriteBufferHighWatermark", read_callbacks_->connection());
  read_callbacks_->connection().readDisable(true);
}

void ConnectionManager::onBelowWriteBufferLowWatermark() {
  ENVOY_CONN_LOG(debug, "onBelowWriteBufferLowWatermark", read_callbacks_->connection());
  read_callbacks_->connection().readDisable(false);
}

StreamHandler& ConnectionManager::newStream() {
  ENVOY_LOG(debug, "dubbo: create the new decoder event handler");

  ActiveMessagePtr new_message(std::make_unique<ActiveMessage>(*this));
  new_message->createFilterChain();
  LinkedList::moveIntoList(std::move(new_message), active_message_list_);
  return **active_message_list_.begin();
}

void ConnectionManager::onHeartbeat(MessageMetadataSharedPtr metadata) {
  stats_.request_event_.inc();

  if (read_callbacks_->connection().state() != Network::Connection::State::Open) {
    ENVOY_LOG(warn, "dubbo: downstream connection is closed or closing");
    return;
  }

  metadata->setResponseStatus(ResponseStatus::Ok);
  metadata->setMessageType(MessageType::HeartbeatResponse);

  HeartbeatResponse heartbeat;
  Buffer::OwnedImpl response_buffer;
  heartbeat.encode(*metadata, *protocol_, response_buffer);

  read_callbacks_->connection().write(response_buffer, false);
}

void ConnectionManager::dispatch() {
  if (0 == request_buffer_.length()) {
    ENVOY_LOG(warn, "dubbo: it's empty data");
    return;
  }

  try {
    bool underflow = false;
    while (!underflow) {
      decoder_->onData(request_buffer_, underflow);
    }
    return;
  } catch (const EnvoyException& ex) {
    ENVOY_CONN_LOG(error, "dubbo error: {}", read_callbacks_->connection(), ex.what());
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    stats_.request_decoding_error_.inc();
  }
  resetAllMessages(true);
}

void ConnectionManager::sendLocalReply(MessageMetadata& metadata,
                                       const DubboFilters::DirectResponse& response,
                                       bool end_stream) {
  if (read_callbacks_->connection().state() != Network::Connection::State::Open) {
    return;
  }

  DubboFilters::DirectResponse::ResponseType result =
      DubboFilters::DirectResponse::ResponseType::ErrorReply;

  try {
    Buffer::OwnedImpl buffer;
    result = response.encode(metadata, *protocol_, buffer);
    read_callbacks_->connection().write(buffer, end_stream);
  } catch (const EnvoyException& ex) {
    ENVOY_CONN_LOG(error, "dubbo error: {}", read_callbacks_->connection(), ex.what());
  }

  if (end_stream) {
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }

  switch (result) {
  case DubboFilters::DirectResponse::ResponseType::SuccessReply:
    stats_.local_response_success_.inc();
    break;
  case DubboFilters::DirectResponse::ResponseType::ErrorReply:
    stats_.local_response_error_.inc();
    break;
  case DubboFilters::DirectResponse::ResponseType::Exception:
    stats_.local_response_business_exception_.inc();
    break;
  default:
    IS_ENVOY_BUG("unexpected status");
  }
}

void ConnectionManager::deferredMessage(ActiveMessage& message) {
  if (!message.inserted()) {
    return;
  }

  message.streamInfo().onRequestComplete();
  //stream_info_.addBytesReceived(1);
   // stream_info_.addBytesSent(2);


   ENVOY_LOG(debug, "active message end, start acess log");

  Http::RequestHeaderMapPtr request_headers = Http::RequestHeaderMapImpl::create();
  Http::RequestHeaderMap& rh = *(request_headers.get());
  const Http::RequestHeaderMap* p_headers = request_headers.get();
  if(message.metadata() && message.metadata()->hasInvocationInfo()){
    const auto invocation = dynamic_cast<const RpcInvocationImpl*>(&(message.metadata()->invocationInfo()));

    if(invocation->hasAttachment()){
      //Http::HeaderMapImpl::copyFrom(*((Http::HeaderMap*)p_headers), invocation->attachment().headers()); 
      invocation->attachment().headers().iterate([&rh](const Http::HeaderEntry& header)->Http::HeaderMap::Iterate {
        // TODO(mattklein123) PERF: Avoid copying here if not necessary.
        Http::HeaderString key_string;
        key_string.setCopy(header.key().getStringView());
        Http::HeaderString value_string;
        value_string.setCopy(header.value().getStringView());

        rh.addViaMove(std::move(key_string), std::move(value_string));
        return Http::HeaderMap::Iterate::Continue;
      });
    }
    request_headers->addCopy(Http::LowerCaseString("X-ENVOY-ORIGINAL-PATH"),  invocation->serviceName());     
  }
 
 
  auto response = Http::ResponseHeaderMapImpl::create();
  const Http::ResponseHeaderMap* response_headers = response.get();
  if(message.hasResponseDecoder() && message.response_decoder().metadata()){
    //auto lowcase_key = Http::LowerCaseString(key);
    //response_headers->remove(lowcase_key);
    //response_headers->addCopy(lowcase_key, value);
  } 
  emitLogEntry(p_headers, response_headers, message.streamInfo());

  read_callbacks_->connection().dispatcher().deferredDelete(
      message.removeFromList(active_message_list_));
}

void ConnectionManager::resetAllMessages(bool local_reset) {
  while (!active_message_list_.empty()) {
    if (local_reset) {
      ENVOY_CONN_LOG(debug, "local close with active request", read_callbacks_->connection());
      stats_.cx_destroy_local_with_active_rq_.inc();
    } else {
      ENVOY_CONN_LOG(debug, "remote close with active request", read_callbacks_->connection());
      stats_.cx_destroy_remote_with_active_rq_.inc();
    }

    active_message_list_.front()->onReset();
  }
}

void ConnectionManager::emitLogEntry(const Http::RequestHeaderMap* request_headers,
                    const Http::ResponseHeaderMap* response_headers,
                    const StreamInfo::StreamInfo& stream_info) { 

  for (const auto& access_log : config_.accessLogs()) {
    ENVOY_LOG(debug, "log access... ");
    access_log->log(request_headers, response_headers, nullptr, stream_info);
  }
}


} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
