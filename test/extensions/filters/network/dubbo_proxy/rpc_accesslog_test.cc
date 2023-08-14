#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.validate.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/app_exception.h"
#include "source/extensions/filters/network/dubbo_proxy/config.h"
#include "source/extensions/filters/network/dubbo_proxy/conn_manager.h"
#include "source/extensions/filters/network/dubbo_proxy/dubbo_hessian2_serializer_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/dubbo_protocol_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/message_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/router/rds_impl.h"


#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/access_loggers/common/file_access_log_impl.h"
#include "source/extensions/access_loggers/file/config.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/network/address_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/extensions/filters/network/dubbo_proxy/mocks.h"
#include "test/extensions/filters/network/dubbo_proxy/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_time.h"

#include "envoy/stream_info/filter_state.h"


#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using Envoy::StreamInfo::UpstreamInfoImpl;
using Envoy::StreamInfo::ResponseCodeDetails;
using Envoy::StreamInfo::StreamInfoImpl;


namespace Envoy {
namespace StreamInfo {

class TestIntAccessor : public FilterState::Object {
public:
  TestIntAccessor(int value) : value_(value) {}

  int access() const { return value_; }

private:
  int value_;
};

} // namespace StreamInfo
} // namespace Envoy



namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

using ConfigDubboProxy = envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy;

class TestAccesslogContext {
public:
  TestAccesslogContext(Server::Configuration::MockFactoryContext& context, const std::string& accesslog_yaml)
      : context_(context) {

        std::shared_ptr<Network::ConnectionInfoSetterImpl> conn = create_connection();
        
        stream_info_ = std::make_shared<StreamInfoImpl>(Http::Protocol::Http2, context.mainThreadDispatcher().timeSource(), conn);
        GTEST_LOG_(INFO) << "init stream_info_"; 
        initAccesslog(accesslog_yaml); 
  }

  std::shared_ptr<Network::ConnectionInfoSetterImpl> create_connection(){

     Network::Address::InstanceConstSharedPtr local =
      Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
    Network::Address::InstanceConstSharedPtr remote =
        Network::Utility::parseInternetAddress("10.20.30.40", 456, false);
    Network::Address::InstanceConstSharedPtr upstream_address =
        Network::Utility::parseInternetAddress("10.1.2.3", 679, false);
    Network::Address::InstanceConstSharedPtr upstream_local_address =
        Network::Utility::parseInternetAddress("10.1.2.3", 1000, false);
    const std::string sni_name = "kittens.com";    

    std::shared_ptr<Network::ConnectionInfoSetterImpl> connection_info_provider = std::make_shared<Network::ConnectionInfoSetterImpl>(
        local, remote);  

    //NiceMock<StreamInfo::MockStreamInfo> info;
    //std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> upstream_host( new NiceMock<Envoy::Upstream::MockHostDescription>());

    //auto downstream_ssl_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
    //auto upstream_ssl_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
    //ConnectionWrapper connection(info);
    //UpstreamWrapper upstream(info);
    //PeerWrapper source(info, false);
    //PeerWrapper destination(info, true);
    //connection_info_provider->setRequestedServerName(sni_name);
    //connection_info_provider->setSslConnection(downstream_ssl_info);

    return connection_info_provider;

  }

  void initAccesslog(const std::string& accesslog_yaml) {
    envoy::extensions::access_loggers::file::v3::FileAccessLog fal_config;
    TestUtility::loadFromYaml(accesslog_yaml, fal_config);

    GTEST_LOG_(INFO) << "accesslog_yaml=" << accesslog_yaml; 

    
    accesslog_config_.mutable_typed_config()->PackFrom(fal_config);

    //file_ = std::make_shared<AccessLog::MockAccessLogFile>();
    Filesystem::FilePathAndType file_info{Filesystem::DestinationType::File, fal_config.path()};

    GTEST_LOG_(INFO) << "init accesslog mock, path=" << fal_config.path();    
    
    stream_info_->protocol(Http::Protocol::Http10);
    stream_info_->setResponseCode(Http::Code::OK);
    stream_info_->setResponseFlag(StreamInfo::ResponseFlag::UpstreamConnectionFailure);    

    stream_info_->setAttemptCount(93);
    stream_info_->setResponseCodeDetails(ResponseCodeDetails::get().ViaUpstream);
    stream_info_->setConnectionTerminationDetails("access_denied");

    stream_info_->setUpstreamInfo(std::make_shared<UpstreamInfoImpl>());
    host_description->hostname_ = "myhost";
    stream_info_->upstreamInfo()->setUpstreamHost(host_description);
    auto local_address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance("127.0.0.3", 8443)};
    stream_info_->upstreamInfo()->setUpstreamLocalAddress(local_address);


    stream_info_->healthCheck(true);
    std::shared_ptr<NiceMock<Envoy::Router::MockRoute>> route =
        std::make_shared<NiceMock<Envoy::Router::MockRoute>>();
        Envoy::Router::RouteConstSharedPtr route2 = route;
    stream_info_->route_ = route2;
    stream_info_->setRouteName("dubbo_local");

    stream_info_->filterState()->setData("test", std::make_unique<StreamInfo::TestIntAccessor>(1),
                                       StreamInfo::FilterState::StateType::ReadOnly,
                                       StreamInfo::FilterState::LifeSpan::FilterChain);
    stream_info_->upstreamInfo()->setUpstreamFilterState(stream_info_->filterState());

     const std::string session_id =
        "D62A523A65695219D46FE1FFE285A4C371425ACE421B110B5B8D11D3EB4D5F0B";
    auto ssl_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*ssl_info, sessionId()).WillRepeatedly(testing::ReturnRef(session_id));
    stream_info_->upstreamInfo()->setUpstreamSslConnection(ssl_info);
    stream_info_->upstreamInfo()->setUpstreamConnectionId(12345);
    stream_info_->upstreamInfo()->setUpstreamInterfaceName("lo");



    cluster_info_->name_ = "my cluster";
    cluster_info_->observability_name_ = "my cluster";
    Upstream::ClusterInfoConstSharedPtr cluster_info = cluster_info_;    
    stream_info_->setUpstreamClusterInfo(cluster_info);

    //EXPECT_CALL(stream_info_, upstreamClusterInfo())
    //    .WillRepeatedly(::testing::Return(upstream_cluster_info_));
    
    //EXPECT_CALL(context_.access_log_manager_, createAccessLog(file_info)).WillRepeatedly(Return(file_));

    EXPECT_CALL(*(context_.access_log_manager_.file_.get()), write(_)).WillRepeatedly(Invoke([](absl::string_view got) {
      
        GTEST_LOG_(INFO) << "access log=" << got; 
    }));

  }

  void testLog(bool is_json) {

    AccessLog::InstanceSharedPtr logger = AccessLog::AccessLogFactory::fromProto(accesslog_config_, context_);

    absl::Time abslStartTime =
        TestUtility::parseTime("Dec 18 01:50:34 2018 GMT", "%b %e %H:%M:%S %Y GMT");
    stream_info_->start_time_ = absl::ToChronoTime(abslStartTime);    

    stream_info_->response_code_ = 200;

    GTEST_LOG_(INFO) << "access mock: is_json=" << is_json << "host name=" << host_description->cluster().name() 
      << ", clustername=" << stream_info_->upstreamClusterInfo().value()->observabilityName(); 

    
    Envoy::StreamInfo::StreamInfo &ss = *stream_info_;
    logger->log(&request_headers_, &response_headers_, &response_trailers_, ss);
  }

  Server::Configuration::MockFactoryContext &context_;
  envoy::config::accesslog::v3::AccessLog accesslog_config_;
  Http::TestRequestHeaderMapImpl request_headers_{{":method", "GET"}, {":path", "/bar/foo"}, {"X-REQUEST-ID", "43de45ad6790-ae34"}};
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  //NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  
  std::shared_ptr<StreamInfoImpl> stream_info_;

  std::shared_ptr<Upstream::MockClusterInfo> cluster_info_{new testing::NiceMock<Upstream::MockClusterInfo>()};
  
  //absl::optional<Upstream::ClusterInfoConstSharedPtr> upstream_cluster_info_ = cluster_info_;

  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host_description{
      new NiceMock<Envoy::Upstream::MockHostDescription>()};

};


class RpcAccesslogTest;
class TestConfigImpl : public ConfigImpl {
public:
  TestConfigImpl(ConfigDubboProxy proto_config, Server::Configuration::MockFactoryContext& context,
                 Router::RouteConfigProviderManager& route_config_provider_manager,
                 DubboFilterStats& stats)
      : ConfigImpl::ConfigImpl(proto_config, context, route_config_provider_manager), stats_(stats){
        
  }
  // ConfigImpl
  DubboFilterStats& stats() override { return stats_; }
  void createFilterChain(DubboFilters::FilterChainFactoryCallbacks& callbacks) override {
    if (setupChain) {
      for (auto& decoder : decoder_filters_) {
        callbacks.addDecoderFilter(decoder);
      }
      for (auto& encoder : encoder_filters_) {
        callbacks.addEncoderFilter(encoder);
      }
      return;
    }

    if (codec_filter_) {
      callbacks.addFilter(codec_filter_);
    }
  }

  void setupFilterChain(int num_decoder_filters, int num_encoder_filters) {
    for (int i = 0; i < num_decoder_filters; i++) {
      decoder_filters_.push_back(std::make_shared<NiceMock<DubboFilters::MockDecoderFilter>>());
    }
    for (int i = 0; i < num_encoder_filters; i++) {
      encoder_filters_.push_back(std::make_shared<NiceMock<DubboFilters::MockEncoderFilter>>());
    }
    setupChain = true;
  }

  void expectFilterCallbacks() {
    for (auto& decoder : decoder_filters_) {
      EXPECT_CALL(*decoder, setDecoderFilterCallbacks(_));
    }
    for (auto& encoder : encoder_filters_) {
      EXPECT_CALL(*encoder, setEncoderFilterCallbacks(_));
    }
  }

  void expectOnDestroy() {
    for (auto& decoder : decoder_filters_) {
      EXPECT_CALL(*decoder, onDestroy());
    }

    for (auto& encoder : encoder_filters_) {
      EXPECT_CALL(*encoder, onDestroy());
    }
  }

  ProtocolPtr createProtocol() override {
    if (protocol_) {
      return ProtocolPtr{protocol_};
    }
    return ConfigImpl::createProtocol();
  }

  Router::RouteConstSharedPtr route(const MessageMetadata& metadata,
                                    uint64_t random_value) const override {
    if (route_) {
      return route_;
    }
    return ConfigImpl::route(metadata, random_value);
  }

  DubboFilters::CodecFilterSharedPtr codec_filter_;
  DubboFilterStats& stats_;
  MockSerializer* serializer_{};
  MockProtocol* protocol_{};
  std::shared_ptr<Router::MockRoute> route_;

  NiceMock<DubboFilters::MockFilterChainFactory> filter_factory_;
  std::vector<std::shared_ptr<DubboFilters::MockDecoderFilter>> decoder_filters_;
  std::vector<std::shared_ptr<DubboFilters::MockEncoderFilter>> encoder_filters_;
  bool setupChain = false;
};

class RpcAccesslogTest : public testing::Test {
public:
  RpcAccesslogTest()
      : stats_(DubboFilterStats::generateStats("test.", store_)),
        engine_(std::make_unique<Regex::GoogleReEngine>()) {

    route_config_provider_manager_ =
        std::make_unique<Router::RouteConfigProviderManagerImpl>(factory_context_.admin_);
  }
  ~RpcAccesslogTest() override {
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  }

  TimeSource& timeSystem() { return factory_context_.mainThreadDispatcher().timeSource(); }

  void initializeFilter() {

    const std::string yaml = R"EOF(
    stat_prefix: test
    multiple_route_config:
      name: local_route
    dubbo_filters:
      - name: envoy.filters.dubbo.router
    access_log:
      - name: envoy.access_loggers.file
        typed_config:
          "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog"
          path: /tmp/envoy_dubbo_acc.log
          log_format:
            text_format_source:
              inline_string: "[%START_TIME%] \"%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH):256% %PROTOCOL%\" upstream_cluster=%UPSTREAM_CLUSTER% uid=%CLUSTER_METADATA(istio:appid)% h=%REQ(HOST)% %RESPONSE_CODE% %RESPONSE_FLAGS% %ROUTE_NAME% %BYTES_RECEIVED% %BYTES_SENT% %UPSTREAM_WIRE_BYTES_RECEIVED% %UPSTREAM_WIRE_BYTES_SENT% %DURATION% %RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)% \"%REQ(X-FORWARDED-FOR)%\" \"%REQ(USER-AGENT)%\" \"%REQ(X-REQUEST-ID)%\"  \"%REQ(:AUTHORITY)%\"\n"
      - name: accesslog2
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
          path: /dev/null

    )EOF";


    const std::string accesslog_yaml = 
       R"(
  path: "/tmp/envoy_dubbo_acc.log"
  log_format:
    text_format_source:
      inline_string: "plain_text - %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% upstream_cluster=%UPSTREAM_CLUSTER% u_host=%UPSTREAM_HOST% h=%REQ(HOST)% res_code=%RESPONSE_CODE% res_f=%RESPONSE_FLAGS% rn=%ROUTE_NAME% req: rid=%REQ(X-REQUEST-ID)%"
)";

    accesslog_context_ = std::make_shared<TestAccesslogContext>(factory_context_, accesslog_yaml); 

    initializeFilter(yaml);
  }

  void initializeFilter(const std::string& yaml) {
    for (const auto& counter : store_.counters()) {
      counter->reset();
    }

    Logger::Registry::setLogLevel(spdlog::level::debug);

    if (!yaml.empty()) {
      TestUtility::loadFromYaml(yaml, proto_config_);
      TestUtility::validate(proto_config_);
    }

    proto_config_.set_stat_prefix("test");
    config_ = std::make_unique<TestConfigImpl>(proto_config_, factory_context_,
                                               *route_config_provider_manager_, stats_);

    accesslog_context_->testLog(false);
    
    if (custom_serializer_) {
      config_->serializer_ = custom_serializer_;
    }
    if (custom_protocol_) {
      config_->protocol_ = custom_protocol_;
    }

    ON_CALL(random_, random()).WillByDefault(Return(42));
    conn_manager_ = std::make_unique<ConnectionManager>(
        *config_, random_, filter_callbacks_.connection_.dispatcher_.timeSource());
    conn_manager_->initializeReadFilterCallbacks(filter_callbacks_);
    conn_manager_->onNewConnection();

    // NOP currently.
    conn_manager_->onAboveWriteBufferHighWatermark();
    conn_manager_->onBelowWriteBufferLowWatermark();
  }

  void writeHessianErrorResponseMessage(Buffer::Instance& buffer, bool is_event,
                                        int64_t request_id) {
    uint8_t msg_type = 0x42; // request message, two_way, not event

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x46});                     // Response status
    addInt64(buffer, request_id);                      // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x06,     // Body Length
                           '\x91',                     // return type, exception
                           0x05, 't', 'e', 's', 't'}); // return body
  }

  void writeHessianExceptionResponseMessage(Buffer::Instance& buffer, bool is_event,
                                            int64_t request_id) {
    uint8_t msg_type = 0x42; // request message, two_way, not event

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x14});
    addInt64(buffer, request_id);                      // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x06,     // Body Length
                           '\x90',                     // return type, exception
                           0x05, 't', 'e', 's', 't'}); // return body
  }

  void writeInvalidResponseMessage(Buffer::Instance& buffer) {
    buffer.add(std::string{
        '\xda', '\xbb', 0x43, 0x14, // Response Message Header, illegal serialization id
        0x00,   0x00,   0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // Request Id
        0x00,   0x00,   0x00, 0x06,                         // Body Length
        '\x94',                                             // return type
        0x05,   't',    'e',  's',  't',                    // return body
    });
  }

  void writeInvalidRequestMessage(Buffer::Instance& buffer) {
    buffer.add(std::string{
        '\xda', '\xbb', '\xc3', 0x00, // Response Message Header, illegal serialization id
        0x00,   0x00,   0x00,   0x00, 0x00, 0x00, 0x00, 0x01, // Request Id
        0x00,   0x00,   0x00,   0x16,                         // Body Length
        0x05,   '2',    '.',    '0',  '.',  '2',              // Dubbo version
        0x04,   't',    'e',    's',  't',                    // Service name
        0x05,   '0',    '.',    '0',  '.',  '0',              // Service version
        0x04,   't',    'e',    's',  't',                    // method name
    });
  }

  void writePartialHessianResponseMessage(Buffer::Instance& buffer, bool is_event,
                                          int64_t request_id, bool start) {

    uint8_t msg_type = 0x42; // request message, two_way, not event

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    if (start) {
      buffer.add(std::string{'\xda', '\xbb'});
      buffer.add(static_cast<void*>(&msg_type), 1);
      buffer.add(std::string{0x14});
      addInt64(buffer, request_id);                  // Request Id
      buffer.add(std::string{0x00, 0x00, 0x00, 0x06, // Body Length
                             '\x94'});               // return type, exception
    } else {
      buffer.add(std::string{0x05, 't', 'e', 's', 't'}); // return body
    }
  }

  void writeHessianResponseMessage(Buffer::Instance& buffer, bool is_event, int64_t request_id) {
    uint8_t msg_type = 0x42; // request message, two_way, not event

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x14});
    addInt64(buffer, request_id);                              // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x06,             // Body Length
                           '\x94', 0x05, 't', 'e', 's', 't'}); // return type, exception
  }

  void writePartialHessianRequestMessage(Buffer::Instance& buffer, bool is_one_way, bool is_event,
                                         int64_t request_id, bool start) {
    uint8_t msg_type = 0xc2; // request message, two_way, not event
    if (is_one_way) {
      msg_type = msg_type & 0xbf;
    }

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    if (start) {
      buffer.add(std::string{'\xda', '\xbb'});
      buffer.add(static_cast<void*>(&msg_type), 1);
      buffer.add(std::string{0x00});
      addInt64(buffer, request_id);                           // Request Id
      buffer.add(std::string{0x00, 0x00, 0x00, 0x16,          // Body Length
                             0x05, '2', '.', '0', '.', '2'}); // Dubbo version
    } else {
      buffer.add(std::string{
          0x04, 't', 'e', 's', 't',      // Service name
          0x05, '0', '.', '0', '.', '0', // Service version
          0x04, 't', 'e', 's', 't',      // method name
      });
    }
  }

  void writeHessianRequestMessage(Buffer::Instance& buffer, bool is_one_way, bool is_event,
                                  int64_t request_id) {
    uint8_t msg_type = 0xc2; // request message, two_way, not event
    if (is_one_way) {
      msg_type = msg_type & 0xbf;
    }

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x00});
    addInt64(buffer, request_id);                            // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x16,           // Body Length
                           0x05, '2',  '.',  '0',  '.', '2', // Dubbo version
                           0x04, 't',  'e',  's',  't',      // Service name
                           0x05, '0',  '.',  '0',  '.', '0', // Service version
                           0x04, 't',  'e',  's',  't'});    // method name
  }

  void writeHessianHeartbeatRequestMessage(Buffer::Instance& buffer, int64_t request_id) {
    uint8_t msg_type = 0xc2; // request message, two_way, not event
    msg_type = msg_type | 0x20;

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x14});
    addInt64(buffer, request_id);                    // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x01}); // Body Length
    buffer.add(std::string{0x01});                   // Body
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  Stats::TestUtil::TestStore store_;
  DubboFilterStats stats_;
  ConfigDubboProxy proto_config_;

  std::unique_ptr<Router::RouteConfigProviderManagerImpl> route_config_provider_manager_;
  std::unique_ptr<TestConfigImpl> config_;

  Buffer::OwnedImpl buffer_;
  Buffer::OwnedImpl write_buffer_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Random::MockRandomGenerator> random_;
  std::unique_ptr<ConnectionManager> conn_manager_;
  MockSerializer* custom_serializer_{};
  MockProtocol* custom_protocol_{};
  ScopedInjectableLoader<Regex::Engine> engine_;

  std::shared_ptr<TestAccesslogContext> accesslog_context_;  
};

TEST_F(RpcAccesslogTest, OnDataHandlesRequestTwoWay) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_twoway").value());
  EXPECT_EQ(0U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(0U, store_.counter("test.request_event").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}


TEST_F(RpcAccesslogTest, OnDataHandlesRequestOneWay) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, true, false, 0x0F);

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.counter("test.request_twoway").value());
  EXPECT_EQ(1U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(0U, store_.counter("test.request_event").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
  EXPECT_EQ(0U, store_.counter("test.response").value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

/*
TEST_F(RpcAccesslogTest, OnDataHandlesHeartbeatEvent) {
  initializeFilter();
  writeHessianHeartbeatRequestMessage(buffer_, 0x0F);

  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        ProtocolPtr protocol = conn_manager_->config().createProtocol();
        MessageMetadataSharedPtr metadata(std::make_shared<MessageMetadata>());
        auto result = protocol->decodeHeader(buffer, metadata);
        EXPECT_TRUE(result.second);
        const DubboProxy::ContextImpl& ctx = *static_cast<const ContextImpl*>(result.first.get());
        EXPECT_TRUE(ctx.isHeartbeat());
        EXPECT_TRUE(metadata->hasResponseStatus());
        EXPECT_FALSE(metadata->isTwoWay());
        EXPECT_EQ(ProtocolType::Dubbo, metadata->protocolType());
        EXPECT_EQ(metadata->responseStatus(), ResponseStatus::Ok);
        EXPECT_EQ(metadata->messageType(), MessageType::HeartbeatResponse);
        buffer.drain(ctx.headerSize());
      }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, buffer_.length());
  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(0U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_event").value());
}

TEST_F(RpcAccesslogTest, HandlesHeartbeatWithException) {
  custom_protocol_ = new NiceMock<MockProtocol>();
  initializeFilter();

  EXPECT_CALL(*custom_protocol_, encode(_, _, _, _)).WillOnce(Return(false));

  MessageMetadataSharedPtr meta = std::make_shared<MessageMetadata>();
  EXPECT_THROW_WITH_MESSAGE(conn_manager_->onHeartbeat(meta), EnvoyException,
                            "failed to encode heartbeat message");
}

TEST_F(RpcAccesslogTest, OnDataHandlesMessageSplitAcrossBuffers) {
  initializeFilter();
  writePartialHessianRequestMessage(buffer_, false, false, 0x0F, true);

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0, buffer_.length());

  // Complete the buffer
  writePartialHessianRequestMessage(buffer_, false, false, 0x0F, false);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  EXPECT_EQ(1U, store_.counter("test.request_twoway").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
}

TEST_F(RpcAccesslogTest, OnDataHandlesProtocolError) {
  initializeFilter();
  writeInvalidRequestMessage(buffer_);

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(0, buffer_.length());

  // Sniffing is now disabled.
  bool one_way = true;
  writeHessianRequestMessage(buffer_, one_way, false, 0x0F);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, store_.counter("test.request").value());
}

TEST_F(RpcAccesslogTest, OnDataHandlesProtocolErrorOnWrite) {
  initializeFilter();
  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto decoder_filter = config_->decoder_filters_[0];

  // Start the read buffer
  writePartialHessianRequestMessage(buffer_, false, false, 0x0F, true);

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  // Disable sniffing
  writeInvalidRequestMessage(write_buffer_);

  callbacks->startUpstreamResponse();

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_NE(DubboFilters::UpstreamResponseStatus::Complete, callbacks->upstreamData(write_buffer_));
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
}

TEST_F(RpcAccesslogTest, OnDataStopsSniffingWithTooManyPendingCalls) {
  initializeFilter();
  config_->setupFilterChain(1, 0);
  // config_->expectOnDestroy();
  auto decoder_filter = config_->decoder_filters_[0];

  int request_count = 64;
  for (int i = 0; i < request_count; i++) {
    writeHessianRequestMessage(buffer_, false, false, i);
  }

  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_)).Times(request_count);
  EXPECT_CALL(*decoder_filter, onDestroy()).Times(request_count);
  EXPECT_CALL(*decoder_filter, onMessageDecoded(_, _)).Times(request_count);

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(64U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  // Sniffing is now disabled.
  writeInvalidRequestMessage(buffer_);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(RpcAccesslogTest, OnWriteHandlesResponse) {
  uint64_t request_id = 100;
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, request_id);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  writeHessianResponseMessage(write_buffer_, false, request_id);

  callbacks->startUpstreamResponse();

  EXPECT_EQ(callbacks->requestId(), request_id);
  EXPECT_EQ(callbacks->connection(), &(filter_callbacks_.connection_));
  EXPECT_GE(callbacks->streamId(), 0);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(RpcAccesslogTest, HandlesResponseContainExceptionInfo) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 1);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_decoding_success").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  writeHessianExceptionResponseMessage(write_buffer_, false, 1);

  callbacks->startUpstreamResponse();

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(1U, store_.counter("test.response_decoding_success").value());
  EXPECT_EQ(1U, store_.counter("test.response_business_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(RpcAccesslogTest, HandlesResponseError) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 1);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  writeHessianErrorResponseMessage(write_buffer_, false, 1);

  callbacks->startUpstreamResponse();

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(1U, store_.counter("test.response_error").value());
  EXPECT_EQ(0U, store_.counter("test.response_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(RpcAccesslogTest, OnWriteHandlesResponseException) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 1);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());

  writeInvalidRequestMessage(write_buffer_);

  callbacks->startUpstreamResponse();

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Reset, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(1U, store_.counter("test.local_response_business_exception").value());
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());
}

// Tests stop iteration/resume with multiple filters.
TEST_F(RpcAccesslogTest, OnDataResumesWithNextFilter) {
  initializeFilter();

  config_->setupFilterChain(2, 0);
  config_->expectOnDestroy();
  auto first_filter = config_->decoder_filters_[0];
  auto second_filter = config_->decoder_filters_[1];

  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*first_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*second_filter, setDecoderFilterCallbacks(_));

  // First filter stops iteration.
  {
    EXPECT_CALL(*first_filter, onMessageDecoded(_, _))
        .WillOnce(Return(FilterStatus::StopIteration));
    EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
    EXPECT_EQ(0U, store_.counter("test.request").value());
    EXPECT_EQ(1U,
              store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
  }

  // Resume processing.
  {
    InSequence s;
    EXPECT_CALL(*first_filter, onMessageDecoded(_, _)).WillOnce(Return(FilterStatus::Continue));
    EXPECT_CALL(*second_filter, onMessageDecoded(_, _)).WillOnce(Return(FilterStatus::Continue));
    callbacks->continueDecoding();
  }

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

// Tests multiple filters are invoked in the correct order.
TEST_F(RpcAccesslogTest, OnDataHandlesDubboCallWithMultipleFilters) {
  initializeFilter();

  config_->setupFilterChain(2, 0);
  config_->expectOnDestroy();
  auto first_filter = config_->decoder_filters_[0];
  auto second_filter = config_->decoder_filters_[1];

  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  InSequence s;
  EXPECT_CALL(*first_filter, onMessageDecoded(_, _)).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*second_filter, onMessageDecoded(_, _)).WillOnce(Return(FilterStatus::Continue));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(RpcAccesslogTest, PipelinedRequestAndResponse) {
  initializeFilter();

  config_->setupFilterChain(1, 0);
  auto decoder_filter = config_->decoder_filters_[0];

  writeHessianRequestMessage(buffer_, false, false, 1);
  writeHessianRequestMessage(buffer_, false, false, 2);

  std::list<DubboFilters::DecoderFilterCallbacks*> callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillRepeatedly(Invoke(
          [&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks.push_back(&cb); }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(2U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
  EXPECT_EQ(2U, store_.counter("test.request").value());

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(2);
  EXPECT_CALL(*decoder_filter, onDestroy()).Times(2);

  writeHessianResponseMessage(write_buffer_, false, 0x01);
  callbacks.front()->startUpstreamResponse();
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete,
            callbacks.front()->upstreamData(write_buffer_));
  callbacks.pop_front();
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());

  writeHessianResponseMessage(write_buffer_, false, 0x02);
  callbacks.front()->startUpstreamResponse();
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete,
            callbacks.front()->upstreamData(write_buffer_));
  callbacks.pop_front();
  EXPECT_EQ(2U, store_.counter("test.response").value());
  EXPECT_EQ(2U, store_.counter("test.response_success").value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(RpcAccesslogTest, ResetDownstreamConnection) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  callbacks->resetDownstreamConnection();

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(RpcAccesslogTest, OnEvent) {
  // No active calls
  {
    initializeFilter();
    conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);
    conn_manager_->onEvent(Network::ConnectionEvent::LocalClose);
    EXPECT_EQ(0U, store_.counter("test.cx_destroy_local_with_active_rq").value());
    EXPECT_EQ(0U, store_.counter("test.cx_destroy_remote_with_active_rq").value());
  }

  // Remote close mid-request
  {
    initializeFilter();

    writePartialHessianRequestMessage(buffer_, false, false, 1, true);
    EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
    conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

    EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());
  }

  // Local close mid-request
  {
    initializeFilter();
    writePartialHessianRequestMessage(buffer_, false, false, 1, true);
    EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
    conn_manager_->onEvent(Network::ConnectionEvent::LocalClose);
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

    EXPECT_EQ(1U, store_.counter("test.cx_destroy_local_with_active_rq").value());

    buffer_.drain(buffer_.length());
  }

  // Remote close before response
  {
    initializeFilter();
    writeHessianRequestMessage(buffer_, false, false, 1);
    EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
    conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

    EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());

    buffer_.drain(buffer_.length());
  }

  // Local close before response
  {
    initializeFilter();
    writeHessianRequestMessage(buffer_, false, false, 1);
    EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
    conn_manager_->onEvent(Network::ConnectionEvent::LocalClose);
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

    EXPECT_EQ(1U, store_.counter("test.cx_destroy_local_with_active_rq").value());

    buffer_.drain(buffer_.length());
  }
}
TEST_F(RpcAccesslogTest, ResponseWithUnknownSequenceID) {
  initializeFilter();

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  writeHessianRequestMessage(buffer_, false, false, 1);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  writeHessianResponseMessage(write_buffer_, false, 10);

  callbacks->startUpstreamResponse();

  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Reset, callbacks->upstreamData(write_buffer_));
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());
}

TEST_F(RpcAccesslogTest, OnDataWithFilterSendsLocalReply) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 233333);

  config_->setupFilterChain(2, 0);
  config_->expectOnDestroy();
  auto& first_filter = config_->decoder_filters_[0];
  auto& second_filter = config_->decoder_filters_[1];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*first_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*second_filter, setDecoderFilterCallbacks(_));

  const std::string fake_response("mock dubbo response");
  NiceMock<DubboFilters::MockDirectResponse> direct_response;
  EXPECT_CALL(direct_response, encode(_, _, _))
      .WillOnce(Invoke([&](MessageMetadata& metadata, Protocol&,
                           Buffer::Instance& buffer) -> DubboFilters::DirectResponse::ResponseType {
        // Validate request id.
        EXPECT_EQ(metadata.requestId(), 233333);
        buffer.add(fake_response);
        return DubboFilters::DirectResponse::ResponseType::SuccessReply;
      }));

  // First filter sends local reply.
  EXPECT_CALL(*first_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr, ContextSharedPtr) -> FilterStatus {
        callbacks->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::NoRouteFound);
        callbacks->sendLocalReply(direct_response, false);
        return FilterStatus::AbortIteration;
      }));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ(fake_response, buffer.toString());
      }));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(SerializationType::Hessian2, callbacks->serializationType());
  EXPECT_EQ(ProtocolType::Dubbo, callbacks->protocolType());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.local_response_success").value());
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(RpcAccesslogTest, OnDataWithFilterSendsLocalErrorReply) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 233334);

  config_->setupFilterChain(2, 0);
  config_->expectOnDestroy();
  auto& first_filter = config_->decoder_filters_[0];
  auto& second_filter = config_->decoder_filters_[1];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*first_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*second_filter, setDecoderFilterCallbacks(_));

  const std::string fake_response("mock dubbo response");
  NiceMock<DubboFilters::MockDirectResponse> direct_response;
  EXPECT_CALL(direct_response, encode(_, _, _))
      .WillOnce(Invoke([&](MessageMetadata& metadata, Protocol&,
                           Buffer::Instance& buffer) -> DubboFilters::DirectResponse::ResponseType {
        // Validate request id.
        EXPECT_EQ(metadata.requestId(), 233334);
        buffer.add(fake_response);
        return DubboFilters::DirectResponse::ResponseType::ErrorReply;
      }));

  // First filter sends local reply.
  EXPECT_CALL(*first_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr, ContextSharedPtr) -> FilterStatus {
        callbacks->sendLocalReply(direct_response, false);
        return FilterStatus::AbortIteration;
      }));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ(fake_response, buffer.toString());
      }));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.local_response_error").value());
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(RpcAccesslogTest, TwoWayRequestWithEndStream) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  EXPECT_CALL(*decoder_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr, ContextSharedPtr) -> FilterStatus {
        return FilterStatus::StopIteration;
      }));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(conn_manager_->onData(buffer_, true), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());
}

TEST_F(RpcAccesslogTest, OneWayRequestWithEndStream) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, true, false, 0x0F);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  EXPECT_CALL(*decoder_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr, ContextSharedPtr) -> FilterStatus {
        return FilterStatus::StopIteration;
      }));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(conn_manager_->onData(buffer_, true), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());
}

TEST_F(RpcAccesslogTest, EmptyRequestData) {
  initializeFilter();
  buffer_.drain(buffer_.length());

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(0);
  EXPECT_EQ(conn_manager_->onData(buffer_, true), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(RpcAccesslogTest, StopHandleRequest) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  ON_CALL(*decoder_filter, onMessageDecoded(_, _))
      .WillByDefault(Invoke([&](MessageMetadataSharedPtr, ContextSharedPtr) -> FilterStatus {
        return FilterStatus::StopIteration;
      }));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite))
      .Times(0);
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(0);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, store_.counter("test.cx_destroy_remote_with_active_rq").value());

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
}

TEST_F(RpcAccesslogTest, HandlesHeartbeatEventWithConnectionClose) {
  initializeFilter();
  writeHessianHeartbeatRequestMessage(buffer_, 0x0F);

  EXPECT_CALL(filter_callbacks_.connection_, write(_, false)).Times(0);

  filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(0U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_event").value());
}

TEST_F(RpcAccesslogTest, SendsLocalReplyWithCloseConnection) {
  initializeFilter();

  const std::string fake_response("mock dubbo response");
  NiceMock<DubboFilters::MockDirectResponse> direct_response;
  EXPECT_CALL(direct_response, encode(_, _, _))
      .WillOnce(Invoke([&](MessageMetadata&, Protocol&,
                           Buffer::Instance& buffer) -> DubboFilters::DirectResponse::ResponseType {
        buffer.add(fake_response);
        return DubboFilters::DirectResponse::ResponseType::ErrorReply;
      }));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));

  MessageMetadata metadata;
  conn_manager_->sendLocalReply(metadata, direct_response, true);
  EXPECT_EQ(1U, store_.counter("test.local_response_error").value());

  // The connection closed.
  EXPECT_CALL(direct_response, encode(_, _, _)).Times(0);
  conn_manager_->sendLocalReply(metadata, direct_response, true);
}

TEST_F(RpcAccesslogTest, RoutingSuccess) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  config_->route_ = std::make_shared<Router::MockRoute>();
  EXPECT_EQ(config_->route_, callbacks->route());

  // Use the cache.
  EXPECT_NE(nullptr, callbacks->route());
}

TEST_F(RpcAccesslogTest, RoutingFailure) {
  initializeFilter();
  writePartialHessianRequestMessage(buffer_, false, false, 0x0F, true);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  EXPECT_CALL(*decoder_filter, onMessageDecoded(_, _)).Times(0);

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  // The metadata is nullptr.
  config_->route_ = std::make_shared<Router::MockRoute>();
  EXPECT_EQ(nullptr, callbacks->route());
}

TEST_F(RpcAccesslogTest, ResetStream) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  callbacks->resetStream();
}

TEST_F(RpcAccesslogTest, NeedMoreDataForHandleResponse) {
  uint64_t request_id = 100;
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, request_id);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  writePartialHessianRequestMessage(write_buffer_, false, false, 0x0F, true);

  callbacks->startUpstreamResponse();

  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::MoreData, callbacks->upstreamData(write_buffer_));
}

TEST_F(RpcAccesslogTest, PendingMessageEnd) {
  uint64_t request_id = 100;
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, request_id);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr, ContextSharedPtr) -> FilterStatus {
        return FilterStatus::StopIteration;
      }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(RpcAccesslogTest, Routing) {
  const std::string yaml = R"EOF(
stat_prefix: test
protocol_type: Dubbo
serialization_type: Hessian2
multiple_route_config:
  name: test_routes
  route_config:
    - name: test1
      interface: org.apache.dubbo.demo.DemoService
      routes:
        - match:
            method:
              name:
                safe_regex:
                  regex: "(.*?)"
          route:
              cluster: user_service_dubbo_server
)EOF";

  initializeFilter(yaml);
  writeHessianRequestMessage(buffer_, false, false, 100);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata, ContextSharedPtr) -> FilterStatus {
        auto invo = static_cast<const RpcInvocationBase*>(&metadata->invocationInfo());
        auto data = const_cast<RpcInvocationBase*>(invo);
        data->setServiceName("org.apache.dubbo.demo.DemoService");
        data->setMethodName("test");
        return FilterStatus::StopIteration;
      }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  Router::RouteConstSharedPtr route = callbacks->route();
  EXPECT_NE(nullptr, route);
  EXPECT_NE(nullptr, route->routeEntry());
  EXPECT_EQ("user_service_dubbo_server", route->routeEntry()->clusterName());
}

TEST_F(RpcAccesslogTest, TransportEndWithConnectionClose) {
  initializeFilter();

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  writeHessianRequestMessage(buffer_, false, false, 1);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  writeHessianResponseMessage(write_buffer_, false, 1);

  callbacks->startUpstreamResponse();

  filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);

  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Reset, callbacks->upstreamData(write_buffer_));
  EXPECT_EQ(1U, store_.counter("test.response_error_caused_connection_close").value());
}

TEST_F(RpcAccesslogTest, MessageDecodedReturnStopIteration) {
  initializeFilter();

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  // The sendLocalReply is not called and the message type is not oneway,
  // the ActiveMessage object is not destroyed.
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(0);

  writeHessianRequestMessage(buffer_, false, false, 1);

  size_t buf_size = buffer_.length();
  EXPECT_CALL(*decoder_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr, ContextSharedPtr ctx) -> FilterStatus {
        EXPECT_EQ(ctx->messageSize(), buf_size);
        return FilterStatus::StopIteration;
      }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  // Buffer data should be consumed.
  EXPECT_EQ(0, buffer_.length());

  // The finalizeRequest should not be called.
  EXPECT_EQ(0U, store_.counter("test.request").value());
}

TEST_F(RpcAccesslogTest, SendLocalReplyInMessageDecoded) {
  initializeFilter();

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  const std::string fake_response("mock dubbo response");
  NiceMock<DubboFilters::MockDirectResponse> direct_response;
  EXPECT_CALL(direct_response, encode(_, _, _))
      .WillOnce(Invoke([&](MessageMetadata&, Protocol&,
                           Buffer::Instance& buffer) -> DubboFilters::DirectResponse::ResponseType {
        buffer.add(fake_response);
        return DubboFilters::DirectResponse::ResponseType::ErrorReply;
      }));
  EXPECT_CALL(*decoder_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr, ContextSharedPtr) -> FilterStatus {
        EXPECT_EQ(1, conn_manager_->getActiveMessagesForTest().size());
        EXPECT_NE(nullptr, conn_manager_->getActiveMessagesForTest().front()->metadata());
        callbacks->sendLocalReply(direct_response, false);
        return FilterStatus::AbortIteration;
      }));

  // The sendLocalReply is called, the ActiveMessage object should be destroyed.
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  writeHessianRequestMessage(buffer_, false, false, 1);

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  // Buffer data should be consumed.
  EXPECT_EQ(0, buffer_.length());

  // The finalizeRequest should be called.
  EXPECT_EQ(1U, store_.counter("test.request").value());
}

TEST_F(RpcAccesslogTest, HandleResponseWithEncoderFilter) {
  uint64_t request_id = 100;
  initializeFilter();

  writeHessianRequestMessage(buffer_, false, false, request_id);

  config_->setupFilterChain(1, 1);
  auto& decoder_filter = config_->decoder_filters_[0];
  auto& encoder_filter = config_->encoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_CALL(*encoder_filter, setEncoderFilterCallbacks(_));

  EXPECT_CALL(*decoder_filter, onDestroy());

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  writeHessianResponseMessage(write_buffer_, false, request_id);

  callbacks->startUpstreamResponse();

  EXPECT_EQ(callbacks->requestId(), request_id);
  EXPECT_EQ(callbacks->connection(), &(filter_callbacks_.connection_));
  EXPECT_GE(callbacks->streamId(), 0);

  size_t expect_response_length = write_buffer_.length();
  EXPECT_CALL(*encoder_filter, onMessageEncoded(_, _))
      .WillOnce(
          Invoke([&](MessageMetadataSharedPtr metadata, ContextSharedPtr ctx) -> FilterStatus {
            EXPECT_EQ(metadata->requestId(), request_id);
            EXPECT_EQ(ctx->messageSize(), expect_response_length);
            return FilterStatus::Continue;
          }));

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete, callbacks->upstreamData(write_buffer_));
  EXPECT_CALL(*encoder_filter, onDestroy());
  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
}

TEST_F(RpcAccesslogTest, HandleResponseWithCodecFilter) {
  uint64_t request_id = 100;
  initializeFilter();
  config_->codec_filter_ = std::make_unique<DubboFilters::MockCodecFilter>();
  auto mock_codec_filter =
      static_cast<DubboFilters::MockCodecFilter*>(config_->codec_filter_.get());

  writeHessianRequestMessage(buffer_, false, false, request_id);

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*mock_codec_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*mock_codec_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata, ContextSharedPtr) -> FilterStatus {
        EXPECT_EQ(metadata->requestId(), request_id);
        return FilterStatus::Continue;
      }));

  EXPECT_CALL(*mock_codec_filter, setEncoderFilterCallbacks(_));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  writeHessianResponseMessage(write_buffer_, false, request_id);

  callbacks->startUpstreamResponse();

  EXPECT_EQ(callbacks->requestId(), request_id);
  EXPECT_EQ(callbacks->connection(), &(filter_callbacks_.connection_));
  EXPECT_GE(callbacks->streamId(), 0);

  size_t expect_response_length = write_buffer_.length();
  EXPECT_CALL(*mock_codec_filter, onMessageEncoded(_, _))
      .WillOnce(
          Invoke([&](MessageMetadataSharedPtr metadata, ContextSharedPtr ctx) -> FilterStatus {
            EXPECT_EQ(metadata->requestId(), request_id);
            EXPECT_EQ(ctx->messageSize(), expect_response_length);
            return FilterStatus::Continue;
          }));

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete, callbacks->upstreamData(write_buffer_));
  EXPECT_CALL(*mock_codec_filter, onDestroy());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
}

TEST_F(RpcAccesslogTestc, AddDataWithStopAndContinue) {
  InSequence s;
  initializeFilter();
  config_->setupFilterChain(3, 3);

  uint64_t request_id = 100;

  EXPECT_CALL(*config_->decoder_filters_[0], onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata, ContextSharedPtr) -> FilterStatus {
        EXPECT_EQ(metadata->requestId(), request_id);
        return FilterStatus::Continue;
      }));
  EXPECT_CALL(*config_->decoder_filters_[1], onMessageDecoded(_, _))
      .WillOnce(Return(FilterStatus::StopIteration))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*config_->decoder_filters_[2], onMessageDecoded(_, _))
      .WillOnce(Return(FilterStatus::Continue));
  writeHessianRequestMessage(buffer_, false, false, request_id);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  config_->decoder_filters_[1]->callbacks_->continueDecoding();

  // For encode direction
  EXPECT_CALL(*config_->encoder_filters_[0], onMessageEncoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata, ContextSharedPtr) -> FilterStatus {
        EXPECT_EQ(metadata->requestId(), request_id);
        return FilterStatus::Continue;
      }));
  EXPECT_CALL(*config_->encoder_filters_[1], onMessageEncoded(_, _))
      .WillOnce(Return(FilterStatus::StopIteration))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*config_->encoder_filters_[2], onMessageEncoded(_, _))
      .WillOnce(Return(FilterStatus::Continue));

  writeHessianResponseMessage(write_buffer_, false, request_id);
  config_->decoder_filters_[0]->callbacks_->startUpstreamResponse();
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete,
            config_->decoder_filters_[0]->callbacks_->upstreamData(write_buffer_));

  config_->encoder_filters_[1]->callbacks_->continueEncoding();
  config_->expectOnDestroy();
}
*/
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
