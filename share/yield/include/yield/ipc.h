// Copyright (c) 2010 Minor Gordon
// With original implementations and ideas contributed by Felix Hupfeld
// All rights reserved
// 
// This source file is part of the Yield project.
// It is licensed under the New BSD license:
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
// * Neither the name of the Yield project nor the
// names of its contributors may be used to endorse or promote products
// derived from this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL Minor Gordon BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


#ifndef _YIELD_IPC_H_
#define _YIELD_IPC_H_

#include "yield/concurrency.h"

#include <queue>
#include <stack>

struct UriUriStructA;
struct yajl_gen_t;
typedef struct yajl_gen_t* yajl_gen;
struct yajl_handle_t;
typedef struct yajl_handle_t* yajl_handle;


#define YIELD_IPC_ONCRPC_ERROR( ClassName, ErrorCode, ErrorMessage ) \
    class ONCRPC ## ClassName ## Error : public Exception\
    {\
    public: \
      const static uint32_t ERROR_CODE = ErrorCode;\
      ONCRPC ## ClassName ## Error()\
        : Exception( ERROR_CODE, "ONC-RPC:" # ErrorMessage )\
      { }\
    };\


namespace yield
{
  namespace ipc
  {
    class HTTPResponse;
    class JSONRPCRequest;
    class JSONRPCResponse;
    class JSONValue;
    class ONCRPCResponse;
    class URI;

    using std::multimap;
    using std::queue;
    using std::stack;

    using yidl::runtime::Buffer;
    using yidl::runtime::Map;
    using yidl::runtime::MarshallableObject;
    using yidl::runtime::Marshaller;
    using yidl::runtime::Object;
    using yidl::runtime::Sequence;
    using yidl::runtime::Unmarshaller;

    using yield::concurrency::Message;
    using yield::concurrency::MessageFactory;
    using yield::concurrency::EventHandler;
    using yield::concurrency::Exception;
    using yield::concurrency::Request;
    using yield::concurrency::RequestHandler;
    using yield::concurrency::Response;
    using yield::concurrency::ResponseHandler;
    using yield::concurrency::SynchronizedSTLQueue;

    using yield::platform::Buffers;
    using yield::platform::BufferedMarshaller;
    using yield::platform::Log;
    using yield::platform::Mutex;
    using yield::platform::Path;
    using yield::platform::Socket;
    using yield::platform::SocketAddress;
    using yield::platform::socket_t;
#ifdef YIELD_PLATFORM_HAVE_OPENSSL
    using yield::platform::SSLContext;
    using yield::platform::SSLSocket;
#endif
    using yield::platform::StackBuffer;
    using yield::platform::StreamSocket;
    using yield::platform::StringBuffer;
    using yield::platform::TCPSocket;
    using yield::platform::Thread;
    using yield::platform::UDPSocket;
    using yield::platform::Time;
    using yield::platform::XDRMarshaller;
    using yield::platform::XDRUnmarshaller;


    class RPCPeer
    {
    protected:
      RPCPeer( YRO_NEW_REF MessageFactory& message_factory )
        : message_factory( message_factory )
      { }

      virtual ~RPCPeer()
      {
        MessageFactory::dec_ref( message_factory );
      }

      MessageFactory& get_message_factory() const { return message_factory; }

    private:
      MessageFactory& message_factory;
    };


    template <class RPCRequestType, class RPCResponseType>
    class RPCClient : public RPCPeer, public RequestHandler
    {
    protected:
      RPCClient( YRO_NEW_REF MessageFactory& message_factory )
        : RPCPeer( message_factory )
      { }

      virtual ~RPCClient() { }

    protected:
      class RPCResponseHandler : public ResponseHandler
      {
      public:
        RPCResponseHandler( YRO_NEW_REF Request& request ) 
          : request( request ) 
        { }

        ~RPCResponseHandler() { Request::dec_ref( request ); }

        // ResponseHandler
        void handle( Response& response )
        {
          if ( response.get_type_id() == RPCResponseType::TYPE_ID )
          {
            RPCResponseType& rpc_response 
              = static_cast<RPCResponseType&>( response );
            request.respond( rpc_response.get_body().inc_ref() );
            RPCResponseType::dec_ref( rpc_response );
          }
          else if ( response.is_exception() )
            request.respond( response );
          else
            DebugBreak();
        }

      private:
        Request& request;
      };
    };


    class RPCRequest : public Request
    {
    public:
      Request& get_body() const { return body; }

    protected:
      RPCRequest( YRO_NEW_REF Request& body ) : body( body ) { }
      virtual ~RPCRequest() { Request::dec_ref( body ); }

    private:
      Request& body;
    };


    class RPCResponse : public Response
    {
    public:
      Response& get_body() const { return body; }

    protected:
      RPCResponse( YRO_NEW_REF Response& body ) : body( body ) { }
      virtual ~RPCResponse() { Response::dec_ref( body ); }

    private:
      Response& body;
    };


    template <class RPCRequestType, class RPCResponseType>    
    class RPCServer : public RPCPeer
    {
    protected:
      RPCServer
      (
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF EventHandler& request_handler
      )
        : RPCPeer( message_factory ),
          request_handler( request_handler )
      { }

      virtual ~RPCServer() 
      {
        EventHandler::dec_ref( request_handler );
      }
      
    protected:
      class RPCResponseHandler : public ResponseHandler
      {
      public:
        RPCResponseHandler( YRO_NEW_REF RPCRequestType& rpc_request )
          : rpc_request( &rpc_request )
        { }

        ~RPCResponseHandler()
        {
          RPCRequestType::dec_ref( rpc_request );
        }

        // ResponseHandler
        void handle( Response& response )
        {
          rpc_request->respond( response );
          RPCRequestType::dec_ref( rpc_request ); // Have to do this to avoid
          rpc_request = NULL;                     // circular references
        }

      private:
        RPCRequestType* rpc_request;
      };

    protected:
      void handle( YRO_NEW_REF RPCRequestType& rpc_request )
      {
        Request& request = rpc_request.get_body();
        request.set_response_handler( new RPCResponseHandler( rpc_request ) );
        request_handler.handle( request.inc_ref() );
      }

    private:
      EventHandler& request_handler;
    };

    
    class SocketPeer
    {
    protected:
      SocketPeer( Log* error_log = NULL, Log* trace_log = NULL );
      virtual ~SocketPeer();

      Log* get_error_log() const { return error_log; }
      Log* get_trace_log() const { return trace_log; }

    private:
      Log* error_log;
      Log* trace_log;
    };


    class SocketClient : public SocketPeer
    {
    public:
      SocketAddress& get_peername() const { return peername; }

    protected:
      SocketClient
      (
        YRO_NEW_REF SocketAddress& peername,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      virtual ~SocketClient();

    private:
      SocketAddress& peername;
    };


    typedef SocketPeer SocketServer;


    template <class StreamSocketType = StreamSocket>
    class StreamSocketClient : public SocketClient
    {
    public:
      class Configuration : public Object
      {
      public:
        const static uint16_t CONCURRENCY_LEVEL_DEFAULT = 1;
        const static uint64_t CONNECT_TIMEOUT_DEFAULT = 5 * Time::NS_IN_S;
        const static uint16_t RECONNECT_TRIES_MAX_DEFAULT = 2;
        const static uint64_t RECV_TIMEOUT_DEFAULT = 5 * Time::NS_IN_S;
        const static uint64_t SEND_TIMEOUT_DEFAULT = 5 * Time::NS_IN_S;

      public:
        Configuration
        (
          uint16_t concurrency_level = CONCURRENCY_LEVEL_DEFAULT,
          const Time& connect_timeout = CONNECT_TIMEOUT_DEFAULT,
          uint16_t reconnect_tries_max = RECONNECT_TRIES_MAX_DEFAULT,
          const Time& recv_timeout = RECV_TIMEOUT_DEFAULT,
          const Time& send_timeout = SEND_TIMEOUT_DEFAULT
        )
          : concurrency_level( concurrency_level ),
            connect_timeout( connect_timeout ),
            reconnect_tries_max( reconnect_tries_max ),
            recv_timeout( recv_timeout ),
            send_timeout( send_timeout )
        { }

        uint16_t get_concurrency_level() const { return concurrency_level; }
        const Time& get_connect_timeout() const { return connect_timeout; }
        uint16_t get_reconnect_tries_max() const { return reconnect_tries_max; }
        const Time& get_recv_timeout() const { return recv_timeout; }
        const Time& get_send_timeout() const { return send_timeout; }

        // Object
        Configuration& inc_ref() { return Object::inc_ref( *this ); }

      private:
        uint16_t concurrency_level;
        Time connect_timeout;
        uint16_t reconnect_tries_max;
        Time recv_timeout;
        Time send_timeout;
      };

    public:
      Configuration& get_configuration() const { return *configuration; }

    protected:
      class Connection
        : public StreamSocketType::AIOConnectCallback,
          public StreamSocketType::AIORecvCallback,
          public StreamSocketType::AIOSendCallback
      {
      public:
        Connection( YRO_NEW_REF StreamSocketType&, StreamSocketClient& );
        virtual ~Connection();

        void close();

      protected:
        void aio_recv( YRO_NEW_REF Buffer&, void* = 0, int flags = 0 );
        void aio_sendmsg( YRO_NEW_REF Buffers&, void* = 0, int flags = 0 );

#ifdef _DEBUG
        void assert_single_threaded();
#else
        inline void assert_single_threaded() const { }
#endif

        Log* get_error_log() const { return error_log; }
        SocketAddress& get_peername() const { return peername; }
        StreamSocketType& get_stream_socket() const { return stream_socket; }
        Log* get_trace_log() const { return trace_log; }
        uint16_t get_remaining_connect_tries();
        virtual void onError( uint32_t error_code, void* context );
        void reset_connect_tries();

        // Socket::AIOConnectCallback
        virtual void onConnectCompletion( size_t bytes_written, void* );
        virtual void onConnectError( uint32_t error_code, void* context );

        // Socket::AIORecvCallback
        virtual void onReadError( uint32_t error_code, void* context );

        // Socket::AIOSendCallback
        virtual void onWriteError( uint32_t error_code, void* context );

      private:
        Time connect_timeout;
        int16_t connect_tries;
        Log* error_log;
        SocketAddress& peername;
        uint16_t reconnect_tries_max;
        Time recv_timeout;
        Time send_timeout;
#ifdef _DEBUG
        unsigned long single_thread_id;
#endif
        StreamSocketType& stream_socket;
        Log* trace_log;
      };


      template <class ConnectionType>
      class ConnectionQueue : private SynchronizedSTLQueue<ConnectionType*>
      {
      public:
        ConnectionQueue( uint16_t concurrency_level )
          : concurrency_level( concurrency_level )
        { }

        virtual ~ConnectionQueue()
        {
          vector<ConnectionType*> connections;

          for ( uint16_t i = 0; i < concurrency_level; i++ )
          {
            ConnectionType& connection = dequeue();
            connection.close();
            connections.push_back( &connection );
          }

          Thread::nanosleep( 0.1 ); // Allow Connections to get out of enqueue

          while ( !connections.empty() )
          {
            ConnectionType::dec_ref( *connections.back() );
            connections.pop_back();
          }
        }

        ConnectionType& dequeue()
        {
          return *SynchronizedSTLQueue<ConnectionType*>::dequeue();
        }

        void enqueue( ConnectionType& connection )
        {
          SynchronizedSTLQueue<ConnectionType*>::enqueue( &connection );
        }

      private:
        uint16_t concurrency_level;
      };

    protected:
      StreamSocketClient
      (
        YRO_NEW_REF SocketAddress& peername,
        YRO_NEW_REF Configuration* configuration = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      virtual ~StreamSocketClient();

    private:
      Configuration* configuration;
    };


    template <class StreamSocketType = StreamSocket>
    class StreamSocketServer 
      : public SocketServer,
        public StreamSocketType::AIOAcceptCallback
    {
    protected:
      StreamSocketServer
      ( 
        YRO_NEW_REF StreamSocketType& listen_stream_socket,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      virtual ~StreamSocketServer();

      StreamSocketType& get_listen_stream_socket() const
      { 
        return listen_stream_socket;
      }

      // StreamSocket::AIOAcceptCallback
      virtual void onAcceptError( uint32_t error_code, void* );

    protected:
      class Connection 
        : public StreamSocketType::AIORecvCallback,          
          public StreamSocketType::AIOSendCallback
      {
      public:
        StreamSocketType& get_stream_socket() const { return stream_socket; }

        // StreamSocket::AIORecvCallback
        virtual void onReadError( uint32_t error_code, void* context );

        // StreamSocket::AIOSendCallback
        virtual void onWriteCompletion( size_t bytes_sent, void* context );
        virtual void onWriteError( uint32_t error_code, void* context );

      protected:
        Connection( YRO_NEW_REF StreamSocketType&, StreamSocketServer& );
        virtual ~Connection();

        void aio_recv( YRO_NEW_REF Buffer&, void* = 0, int flags = 0 );
        void aio_sendmsg( YRO_NEW_REF Buffers&, void* = 0, int flags = 0 );

      private:
        StreamSocketType& stream_socket;
        StreamSocketServer& stream_socket_server;
      };

    private:
      void onReadError( Connection&, uint32_t error_code );
      void onWriteError( Connection&, uint32_t error_code );

    private:
      StreamSocketType& listen_stream_socket;
    };


    class TCPSocketClient : public StreamSocketClient<TCPSocket>
    {
    public:
      static YRO_NEW_REF TCPSocket& createTCPSocket( Log* trace_log = NULL );

    protected:
      TCPSocketClient
      (
        YRO_NEW_REF SocketAddress& peername,
        YRO_NEW_REF Configuration* configuration = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      virtual ~TCPSocketClient() { }
    };


    class TCPSocketServer : public StreamSocketServer<TCPSocket>
    {
    public:
      static YRO_NEW_REF TCPSocket&
      createListenTCPSocket
      (
        const SocketAddress& sockname,
        Log* trace_log = NULL
      );

    protected:
      TCPSocketServer
      ( 
        YRO_NEW_REF TCPSocket& listen_tcp_socket,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      virtual ~TCPSocketServer() { }

      TCPSocket& get_listen_tcp_socket() const;
      static bool init_listen_tcp_socket( TCPSocket&, const SocketAddress& );
    };


#ifdef YIELD_PLATFORM_HAVE_OPENSSL
    class SSLSocketClient : public TCPSocketClient
    {
    public:
      static YRO_NEW_REF SSLSocket&
      createSSLSocket
      (         
        SSLContext* ssl_context = NULL,
        Log* trace_log = NULL
      );

    protected:
      SSLSocketClient
      (
        YRO_NEW_REF SocketAddress& peername,
        YRO_NEW_REF Configuration* configuration = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      virtual ~SSLSocketClient() { }
    };


    class SSLSocketServer : public TCPSocketServer
    {
    public:
      static YRO_NEW_REF SSLSocket&
      createListenSSLSocket
      (
        const SocketAddress& sockname,
        SSLContext& ssl_context,
        Log* trace_log = NULL
      );

    protected:
      SSLSocketServer
      ( 
        YRO_NEW_REF SSLSocket& listen_ssl_socket,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );
    };
#endif


    class UDPSocketClient 
      : public SocketClient,
        public UDPSocket::AIORecvCallback
    {
    public:
      const static uint64_t RECV_TIMEOUT_DEFAULT = 5 * Time::NS_IN_S;

    public:
      static YRO_NEW_REF UDPSocket&
      createConnectedUDPSocket
      ( 
        const URI& absolute_uri,
        Log* trace_log = NULL
      );

      static YRO_NEW_REF UDPSocket& createUDPSocket( Log* trace_log = NULL );

      const Time& get_recv_timeout() const { return recv_timeout; }

    protected:
      UDPSocketClient
      (
        YRO_NEW_REF SocketAddress& peername,
        YRO_NEW_REF UDPSocket& udp_socket,
        Log* error_log = NULL,
        const Time& recv_timeout = RECV_TIMEOUT_DEFAULT,
        Log* trace_log = NULL
      );


      UDPSocket& get_udp_socket() const { return udp_socket; }

      // UDPSocket::AIORecvCallback
      virtual void onReadCompletion( Buffer& buffer, void* context );
      virtual void onReadError( uint32_t error_code, void* context );

    private:
      Time recv_timeout;
      UDPSocket& udp_socket;
    };


    class UDPSocketServer 
      : public SocketServer,
        public UDPSocket::AIORecvFromCallback        
    {
    public:
      static YRO_NEW_REF UDPSocket&
      createBoundUDPSocket
      ( 
        const SocketAddress& sockname,
        Log* trace_log = NULL
      );

    protected:
      UDPSocketServer
      ( 
        YRO_NEW_REF UDPSocket& udp_socket,
        Log* error_log,
        Log* trace_log
      );

      virtual ~UDPSocketServer();

      UDPSocket& get_udp_socket() const { return udp_socket; }

    protected:
      class ResponseHandler : public yield::concurrency::ResponseHandler
      {
      protected:
        ResponseHandler( SocketAddress& peername, UDPSocket& udp_socket );
        virtual ~ResponseHandler();

        const SocketAddress& get_peername() const { return peername; }
        UDPSocket& get_udp_socket() const { return udp_socket; }

      private:
        SocketAddress& peername;
        UDPSocket& udp_socket;
      };

    private:
      UDPSocket& udp_socket;
    };


    class HTTPMessage
    {
    public:
      typedef pair<uint16_t, uint16_t> FieldOffset;
      typedef vector<FieldOffset> FieldOffsets;

    public:
      Buffer* get_body() const { return body; }

      const char*
      get_field
      ( 
        const char* name, 
        const char* default_value = "" 
      ) const;

      Time get_time_field( const char* name ) const;

      const char* operator[]( const char* name ) { return get_field( name ); }

      void set_body( YRO_NEW_REF Buffer* body );
      // set_field: char* copies into a buffer, const char* does not
      void set_field( const char* name, const char* value );
      void set_field( const char* name, char* value );
      void set_field( char* name, char* value );
      void set_field( const string& name, const string& value );
      void set_field( const char* name, const Time& value );

    protected:
      HTTPMessage( YRO_NEW_REF Buffer* body = NULL );

      HTTPMessage
      ( 
        YRO_NEW_REF Buffer& header,
        const FieldOffsets& field_offsets, 
        YRO_NEW_REF Buffer* body = NULL 
      );

      virtual ~HTTPMessage();
      
      Buffers& get_header() const { return *header; }

      virtual YRO_NEW_REF Buffers& marshal() const;

    private:
      template <class, class> friend class HTTPMessageParser;

      static const char* 
      get_field
      (
        Buffer& header,
        const FieldOffsets& field_offsets,
        const char* name,
        const char* default_value = ""
      );

    private:
      Buffer* body;
      FieldOffsets field_offsets;
      Buffers* header;
    };


    template <class HTTPMessageParserType, class HTTPMessageType>
    class HTTPMessageParser
    {
    public:
      HTTPMessageType* parse( const string& buffer ); // For testing

      bool
      parse
      ( 
        Buffer& buffer,
        HTTPMessageType*& out_http_message,
        Buffer** out_next_buffer = NULL
      );

      static Time parse_http_date( const char* http_date );

    protected:
      HTTPMessageParser();
      virtual ~HTTPMessageParser();

    private:
      YRO_NEW_REF HTTPMessageType* createHTTPMessage( Buffer* body = NULL );
      size_t get_content_length();
      bool parse_body( Buffer&, HTTPMessageType*&, Buffer** );
      bool parse_header( Buffer&, HTTPMessageType*&, Buffer** );
      bool parse_header( char** inout_p, const char* pe );
      void reset();

    private:
      // Parse state
      Buffer* body;
      size_t content_length;
      HTTPMessage::FieldOffsets field_offsets;
      Buffer* header;      
    };


    class HTTPRequest : public Request, public HTTPMessage
    {
    public:
      HTTPRequest
      ( 
        const char* method,
        const char* uri,
        YRO_NEW_REF Buffer* body = NULL
      );

      HTTPRequest
      ( 
        const char* method,
        const URI& uri,
        YRO_NEW_REF Buffer* body = NULL
      );

      virtual ~HTTPRequest();

      const Time& get_creation_time() const { return creation_time; }
      double get_http_version() const; // double to allow literal comparisons
      const char* get_method() const;
      URI& get_parsed_uri();
      const char* get_uri() const;

      YRO_NEW_REF Buffers& marshal() const { return HTTPMessage::marshal(); }
      void respond( YRO_NEW_REF HTTPResponse& http_response );
      void respond( uint16_t status_code, YRO_NEW_REF Buffer* body = NULL );
      void respond( YRO_NEW_REF Exception& exception );

      // Object
      HTTPRequest& inc_ref() { return Object::inc_ref( *this ); }

      // MarshallableObject
      const static uint32_t TYPE_ID = 205;
      uint32_t get_type_id() const { return TYPE_ID; }
      const char* get_type_name() const { return "HTTPRequest"; }
      void marshal( Marshaller& marshaller ) const { }
      void unmarshal( Unmarshaller& ) { }

    private:
      friend class HTTPRequestParser;

      HTTPRequest
      (
        YRO_NEW_REF Buffer& header,
        uint16_t method_offset, // Into header
        uint16_t uri_offset,
        uint16_t http_version_offset,
        const FieldOffsets& field_offsets,
        YRO_NEW_REF Buffer* body = NULL
      );

      HTTPRequest( const HTTPRequest& ) { DebugBreak(); } // Prevent copying

      void init( const char* method, const char* uri );

    private:
      Time creation_time;
      uint16_t method_offset, uri_offset, http_version_offset;
      URI* parsed_uri;
    };


    class HTTPRequestHandler : public RequestHandler
    {
    public:
      virtual void handle( YRO_NEW_REF HTTPRequest& http_request ) = 0;

      // RequestHandler
      virtual void handle( YRO_NEW_REF Request& request );
    };


    class HTTPRequestParser 
      : public HTTPMessageParser<HTTPRequestParser, HTTPRequest>
    {
    public:
      HTTPRequestParser();
      
    private:
      template <class, class> friend class HTTPMessageParser;

      // HTTPMessageParser downcalls
      YRO_NEW_REF HTTPRequest*
      createHTTPMessage
      (
        Buffer& header,
        const HTTPMessage::FieldOffsets& field_offsets,
        YRO_NEW_REF Buffer* body = NULL
      );

      bool parse_first_header_line( char** inout_p, const char* pe );

    private:
      uint16_t method_offset, uri_offset, http_version_offset;
    };


    class HTTPResponse : public Response, public HTTPMessage
    {
    public:
      HTTPResponse
      ( 
        uint16_t status_code = 200,
        YRO_NEW_REF Buffer* body = NULL
      );

      virtual ~HTTPResponse() { }

      uint16_t get_status_code() const { return status_code; }
      YRO_NEW_REF Buffers& marshal() const { return HTTPMessage::marshal(); }

      // Object
      HTTPResponse& inc_ref() { return Object::inc_ref( *this ); }

      // MarshallableObject
      const static uint32_t TYPE_ID = 206;
      uint32_t get_type_id() const { return TYPE_ID; }
      const char* get_type_name() const { return "HTTPResponse"; }
      void marshal( Marshaller& marshaller ) const { }
      void unmarshal( Unmarshaller& ) { }

    private:
      friend class HTTPResponseParser;

      HTTPResponse
      (
        Buffer& header,
        const FieldOffsets& field_offsets,
        uint16_t status_code,
        Buffer* body
      );

      HTTPResponse( const HTTPResponse& ) { DebugBreak(); } // Prevent copying

      void init( uint16_t status_code );

    private:
      uint16_t status_code;
    };


    typedef yield::concurrency::ResponseQueue<HTTPResponse> HTTPResponseQueue;


    class HTTPResponseParser 
      : public HTTPMessageParser<HTTPResponseParser, HTTPResponse>
    {
    public:
      HTTPResponseParser();

    private:
      template <class, class> friend class HTTPMessageParser;

      // HTTPMessageParser downcalls
      YRO_NEW_REF HTTPResponse*
      createHTTPMessage
      (
        Buffer& header,
        const HTTPMessage::FieldOffsets& field_offsets,
        YRO_NEW_REF Buffer* body = NULL
      );

      bool parse_first_header_line( char** inout_p, const char* pe );

    private:
      uint16_t status_code;
    };


    class HTTPClient : public HTTPRequestHandler, public TCPSocketClient
    {
    public:
      HTTPClient
      (
        YRO_NEW_REF SocketAddress& peername,
        YRO_NEW_REF TCPSocket& tcp_socket,
        YRO_NEW_REF Configuration* configuration = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      virtual ~HTTPClient() { }

      static YRO_NEW_REF HTTPClient&
      create
      (
        const URI& absolute_uri,
        YRO_NEW_REF Configuration* configuration = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      static YRO_NEW_REF HTTPResponse& GET( const URI& absolute_uri );

      static
      YRO_NEW_REF HTTPResponse& 
      PUT
      ( 
        const URI& absolute_uri,
        YRO_NEW_REF Buffer& body
      );

      static 
      YRO_NEW_REF HTTPResponse&
      PUT
      (
        const URI& absolute_uri,
        const Path& body_file_path
      );

      // Object
      HTTPClient& inc_ref() { return Object::inc_ref( *this ); }

      // HTTPRequestHandler
      void handle( HTTPRequest& request );

    private:
      class Connection 
        : public TCPSocketClient::Connection,
          private HTTPResponseParser
      {
      public:
        Connection( HTTPClient&, YRO_NEW_REF TCPSocket& );

        void handle( YRO_NEW_REF HTTPRequest& http_request );

      private:
        // TCPSocketClient::Connection
        void onError( uint32_t error_code, void* context );

        // TCPSocket::AIORecvCallback
        void onReadCompletion( Buffer& buffer, void* context );

        // TCPSocket::AIOSendCallback
        void onWriteCompletion( size_t bytes_sent, void* context );
        void onWriteError( uint32_t error_code, void* context );

      private:
        HTTPClient::ConnectionQueue<Connection>& connection_queue;
        queue<HTTPRequest*> live_http_requests;
      };

      typedef ConnectionQueue<Connection> ConnectionQueue;
      ConnectionQueue connection_queue;
    };


#ifdef YIELD_PLATFORM_HAVE_OPENSSL
    class HTTPSClient : public HTTPClient
    {
    public:
      HTTPSClient
      (
        YRO_NEW_REF SocketAddress& peername,
        YRO_NEW_REF SSLSocket& ssl_socket,
        YRO_NEW_REF Configuration* configuration = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      virtual ~HTTPSClient() { }

      static HTTPSClient&
      create
      (
        const URI& absolute_uri,
        YRO_NEW_REF Configuration* configuration = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL,
        SSLContext* ssl_context = NULL
      );
    };
#endif


    class HTTPServer : public TCPSocketServer
    {
    public:
      class AccessLog : public Object
      {
      public:
        class Format
        {
        public:
          virtual string
          operator()
          ( 
            const HTTPRequest&,
            const HTTPResponse& 
          ) const = 0;
        };

        class CommonFormat : public Format
        {
        public:
          virtual string
          operator()
          ( 
            const HTTPRequest&,
            const HTTPResponse& 
          ) const;
        };

        class CombinedFormat : public CommonFormat
        {
        public:
          string operator()( const HTTPRequest&, const HTTPResponse& ) const;
        };

      public:
        virtual ~AccessLog();

        static AccessLog&
        open
        ( 
          const Path& file_path,
          Format* format = NULL, // Steals this, defaults to CombinedFormat
          bool lazy_open = false 
        );

        static AccessLog&
        open
        ( 
          ostream&,
          Format* format = NULL // Steals this, defaults to CombinedFormat
        );

        virtual void write( const HTTPRequest&, const HTTPResponse& ) = 0;
    
      protected:
        AccessLog( Format& format );

        Format& get_format() { return format; }

      private:
        Format& format;
      };

    public:
      HTTPServer
      (
        YRO_NEW_REF EventHandler& http_request_handler,
        YRO_NEW_REF TCPSocket& listen_tcp_socket,
        YRO_NEW_REF AccessLog* access_log = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      virtual ~HTTPServer();

      static HTTPServer&
      create
      (
        YRO_NEW_REF EventHandler& http_request_handler,
        const SocketAddress& sockname,
        YRO_NEW_REF AccessLog* access_log = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      // Object
      HTTPServer& inc_ref() { return Object::inc_ref( *this ); }

    private:
      // TCPSocket::AIOAcceptCallback
      virtual void onAcceptCompletion( TCPSocket&, void*, Buffer* );

    private:
      class Connection;
      class FileAccessLog;
      class ostreamAccessLog;

    private:
      AccessLog* access_log;
      EventHandler& http_request_handler;
    };


#ifdef YIELD_PLATFORM_HAVE_OPENSSL
    class HTTPSServer : public HTTPServer
    {
    public:
      HTTPSServer
      (
        YRO_NEW_REF EventHandler& http_request_handler,
        YRO_NEW_REF SSLSocket& listen_ssl_socket,
        YRO_NEW_REF AccessLog* access_log = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      static HTTPSServer&
      create
      (
        YRO_NEW_REF EventHandler& http_request_handler,
        const SocketAddress& sockname,
        SSLContext& ssl_context,
        YRO_NEW_REF AccessLog* access_log = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );
    };
#endif


    class JSONMarshaller : public Marshaller
    {
    public:
      JSONMarshaller();
      virtual ~JSONMarshaller();

      Buffer& get_buffer() const { return *buffer; }

      void write( const JSONRPCRequest& json_rpc_request );
      void write( const JSONRPCResponse& json_rpc_response );
      void write( const char* key, const JSONValue& value );
      virtual void write( const Key& key, const JSONValue& value );
      void write( const MarshallableObject& value ) { write( &value ); }
      void write( const MarshallableObject* value ) { write_object( value ); }
      void write( const Sequence& value ) { write( &value ); }
      void write( const Sequence* value ) { write_array( value ); }      

      void write_array( const char* key, const MarshallableObject* value );
      virtual void write_array( const Key& key, const MarshallableObject* );
      virtual void write_array( const MarshallableObject* value );
      void write_null( const char* key );
      virtual void write_null( const Key& key );
      void write_object( const char* key, const MarshallableObject* value );
      virtual void write_object( const Key& key, const MarshallableObject* );
      virtual void write_object( const MarshallableObject* value );

      // Marshaller
      virtual void write( const Key& key, bool value );
      virtual void write( const Key& key, double value  );
      virtual void write( const Key& key, int64_t value );
      virtual void write( const Key& key, const MarshallableObject& value );
      virtual void write( const Key& key, const Sequence& value );
      virtual void write( const Key& key, const char* value, size_t );

    protected:
      virtual void write( const Key& );

    private:
      void flushYAJLBuffer();

    private:
      Buffer* buffer;
      stack<bool> in_map_stack;
      yajl_gen writer;
    };


    class JSONValue : public Object
    {
    public:
      enum Type 
      { 
         TYPE_ARRAY,
         TYPE_FALSE,
         TYPE_NULL,
         TYPE_NUMBER,
         TYPE_OBJECT,
         TYPE_STRING,
         TYPE_TRUE
      };

      JSONValue( Type type ) : type( type ) { }

      inline Type get_type() const { return type; }

      // Object
      JSONValue& inc_ref() { return Object::inc_ref( *this ); }

    private:
      Type type;
    };


    class JSONArray : public JSONValue, public vector<JSONValue*>
    {
    public:
      JSONArray() : JSONValue( TYPE_ARRAY ) { }
    };


    class JSONNumber : public JSONValue
    {
    public:
      JSONNumber( double value ) : JSONValue( TYPE_NUMBER ), value( value ) { }
      inline operator double() const { return value; }

    private:
      double value;
    };


    class JSONString : public JSONValue
    {
    public:
      JSONString
      ( 
        Buffer& underlying_buffer,
        const unsigned char* value,
        unsigned int value_len
      )
        : JSONValue( TYPE_STRING ),
          underlying_buffer( underlying_buffer.inc_ref() ),
          value( value ),
          value_len( value_len )
      { }

      inline operator const unsigned char*() const { return value; }

      operator const char*() const
      { 
        return reinterpret_cast<const char*>( value ); 
      }

      inline unsigned int size() const { return value_len; }

    private:
      Buffer& underlying_buffer;
      const unsigned char* value;
      unsigned int value_len;
    };


    class JSONObject 
      : public JSONValue,
        public vector< pair<JSONString*, JSONValue*> >
    {
    public:
      JSONObject() : JSONValue( TYPE_OBJECT ) { }

      JSONValue* operator[]( const char* name )
      {
        for ( const_iterator pair_i = begin(); pair_i != end(); ++pair_i )
          if ( strncmp( *pair_i->first, name, pair_i->first->size() ) == 0 )
            return pair_i->second;

        return NULL;
      }
    };


    class JSONParser
    {
    public:
      JSONParser();
      ~JSONParser();

      YRO_NEW_REF JSONValue* parse( Buffer& buffer );

    private:
      void handleJSONValue( JSONValue& json_value );

      // yajl callbacks
      static int handle_yajl_boolean( void*, int value );
      static int handle_yajl_double( void*, double value );
      static int handle_yajl_end_array( void* );
      static int handle_yajl_end_map( void* );
      static int handle_yajl_integer( void*, long value );
      static int handle_yajl_map_key( void*, const uint8_t*, unsigned int );
      static int handle_yajl_null( void* );
      static int handle_yajl_start_array( void* );
      static int handle_yajl_start_map( void* );
      static int handle_yajl_string( void*, const uint8_t*, unsigned int );

    private:
      Buffer* buffer;
      JSONValue *JSONfalse, *JSONnull, *JSONtrue;
      stack<JSONValue*> json_value_stack;
      JSONString* next_map_key;
      yajl_handle reader;      
    };


    class JSONRPCMessage
    {
    public:
      JSONValue& get_id() const { return id; }

    protected:
      JSONRPCMessage( JSONValue& id ) : id( id ) { }
      virtual ~JSONRPCMessage() { JSONValue::dec_ref( id ); }

    private:
      JSONValue& id;
    };


    class JSONRPCMessageParser
    {
    protected:
      JSONRPCMessageParser( MessageFactory& message_factory )
        : message_factory( message_factory.inc_ref() )
      { }

      virtual ~JSONRPCMessageParser()
      {
        MessageFactory::dec_ref( message_factory );
      }

      MessageFactory& get_message_factory() const { return message_factory; }

    private:
      MessageFactory& message_factory;
    };


    class JSONRPCRequest : public RPCRequest, public JSONRPCMessage
    {
    public:
      JSONRPCRequest
      ( 
        YRO_NEW_REF Request& body,
        YRO_NEW_REF HTTPRequest& http_request,
        YRO_NEW_REF JSONValue& id
      );

      virtual ~JSONRPCRequest();

      HTTPRequest& get_http_request() const { return http_request; }
      
      YRO_NEW_REF Buffers& marshal() const;
      void marshal( JSONMarshaller& json_marshaller ) const;
      void respond( YRO_NEW_REF JSONRPCResponse& response );
      void respond( YRO_NEW_REF Response& response );

      // Object
      JSONRPCRequest& inc_ref() { return Object::inc_ref( *this ); }

      // MarshallableObject
      const static uint32_t TYPE_ID = 313;
      uint32_t get_type_id() const { return TYPE_ID; }
      const char* get_type_name() const { return "JSONRPCRequest"; }
      void marshal( Marshaller& marshaller ) const { }
      void unmarshal( Unmarshaller& unmarshaller ) { }

    private:
      HTTPRequest& http_request;
    };

    
    class JSONRPCRequestHandler : public RequestHandler
    {
    public:
      virtual void handle( YRO_NEW_REF JSONRPCRequest& json_rpc_request ) = 0;

      // RequestHandler
      virtual void handle( YRO_NEW_REF Request& request );
    };


    class JSONRPCRequestParser : private JSONRPCMessageParser
    {
    public:
      JSONRPCRequestParser( MessageFactory& message_factory );
      virtual ~JSONRPCRequestParser() { }

      YRO_NEW_REF JSONRPCRequest* parse( YRO_NEW_REF HTTPRequest& );

    private:
      YRO_NEW_REF JSONRPCRequest* parse( Buffer&, YRO_NEW_REF HTTPRequest& );
    };


    class JSONRPCResponse : public RPCResponse, public JSONRPCMessage
    {
    public:
      JSONRPCResponse
      ( 
        YRO_NEW_REF Response& body,
        YRO_NEW_REF HTTPResponse& http_response,
        YRO_NEW_REF JSONValue& id
      );
          
      virtual ~JSONRPCResponse();

      HTTPResponse& get_http_response() const { return http_response; }
      YRO_NEW_REF Buffers& marshal() const;
      void marshal( JSONMarshaller& json_marshaller ) const;

      // Object
      JSONRPCResponse& inc_ref() { return Object::inc_ref( *this ); }

      // MarshallableObject
      const static uint32_t TYPE_ID = 314;
      uint32_t get_type_id() const { return TYPE_ID; }
      const char* get_type_name() const { return "JSONRPCResponse"; }
      void marshal( Marshaller& marshaller ) const { }
      void unmarshal( Unmarshaller& unmarshaller ) { }

    private:
      HTTPResponse& http_response;
    };


    class JSONRPCResponseParser : private JSONRPCMessageParser
    {
    public:
      JSONRPCResponseParser( MessageFactory& message_factory );
      virtual ~JSONRPCResponseParser() { }

      JSONRPCResponse* parse( HTTPResponse&, JSONRPCRequest& );
    };


    class JSONRPCClient 
      : public RPCClient<JSONRPCRequest, JSONRPCResponse>,
        public TCPSocketClient
    {
    public:
      JSONRPCClient
      (
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF SocketAddress& peername,
        const URI& post_uri, // e.g. /JSONRPC
        YRO_NEW_REF TCPSocket& tcp_socket,
        YRO_NEW_REF Configuration* configuration = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      virtual ~JSONRPCClient();

      static JSONRPCClient&
      create
      (
        const URI& absolute_uri,
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF Configuration* configuration = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      // RequestHandler
      virtual void handle( YRO_NEW_REF Request& request );

    private:
      class Connection 
        : public TCPSocketClient::Connection,
          public HTTPResponseParser,
          public JSONRPCResponseParser
      {
      public:
        Connection( JSONRPCClient&, YRO_NEW_REF TCPSocket& );

        void handle( YRO_NEW_REF JSONRPCRequest& );

      private:
        // TCPSocket::AIORecvCallback
        void onReadCompletion( Buffer& buffer, void* context );

        // TCPSocket::AIOSendCallback
        void onWriteCompletion( size_t bytes_sent, void* context );
        void onWriteError( uint32_t error_code, void* context );

        // StreamSocketClient::Connection
        void onError( uint32_t error_code, void* context );

      private:
        JSONRPCClient::ConnectionQueue<Connection>& connection_queue;
        queue<JSONRPCRequest*> live_json_rpc_requests;
      };

      typedef ConnectionQueue<Connection> ConnectionQueue;
      ConnectionQueue connection_queue;

      URI* post_uri;
    };


#ifdef YIELD_PLATFORM_HAVE_OPENSSL
    class JSONRPCSClient : public JSONRPCClient
    {
    public:
      JSONRPCSClient
      (
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF SocketAddress& peername,
        const URI& post_uri, // e.g. /JSONRPC
        YRO_NEW_REF SSLSocket& ssl_socket,
        YRO_NEW_REF Configuration* configuration = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      virtual ~JSONRPCSClient() { }

      static JSONRPCSClient&
      create
      (
        const URI& absolute_uri,
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF Configuration* configuration = NULL,
        Log* error_log = NULL,
        SSLContext* ssl_context = NULL,
        Log* trace_log = NULL
      );
    };
#endif


    class JSONRPCServer 
      : public RPCServer<JSONRPCRequest, JSONRPCResponse>,
        public TCPSocketServer
    {
    public:
      JSONRPCServer
      (
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF TCPSocket& listen_tcp_socket,
        YRO_NEW_REF EventHandler& request_handler,
        YRO_NEW_REF HTTPServer::AccessLog* access_log = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      virtual ~JSONRPCServer() { }

      static JSONRPCServer&
      create
      (
        MessageFactory& message_factory, // Steals this reference
        EventHandler& request_handler, // Steals this reference
        const SocketAddress& sockname,
        YRO_NEW_REF HTTPServer::AccessLog* access_log = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

    private:
      // TCPSocket::AIOAcceptCallback
      virtual void onAcceptCompletion( TCPSocket&, void*, Buffer* );

    private:
      class Connection;

    private:
      HTTPServer::AccessLog* access_log;
    };


#ifdef YIELD_PLATFORM_HAVE_OPENSSL
    class JSONRPCSServer : public JSONRPCServer
    {
    public:
      JSONRPCSServer
      (
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF SSLSocket& listen_ssl_socket,
        YRO_NEW_REF EventHandler& request_handler,
        YRO_NEW_REF HTTPServer::AccessLog* access_log = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      virtual ~JSONRPCSServer() { }

      static JSONRPCSServer&
      create
      (
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF EventHandler& request_handler,
        const SocketAddress& sockname,
        SSLContext& ssl_context,
        YRO_NEW_REF HTTPServer::AccessLog* access_log = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );
    };
#endif


    class JSONUnmarshaller : public Unmarshaller
    {
    public:
      JSONUnmarshaller( const JSONValue& root_json_value );
      virtual ~JSONUnmarshaller() { }

      void read( Map& value );
      void read( MarshallableObject& value );
      void read( Sequence& value );

      // Unmarshaller
      virtual bool read_bool( const Key& key );
      virtual void read( const Key& key, double& value );
      virtual void read( const Key& key, int64_t& value );
      virtual Key* read( Key::Type );
      virtual void read( const Key& key, Map& value );
      virtual void read( const Key& key, MarshallableObject& value );
      virtual void read( const Key& key, Sequence& value );
      virtual void read( const Key& key, string& value );

    private:
      const JSONValue* read( const Key& key );

    private:
      const JSONValue& root_json_value;
      size_t next_json_value_i;
    };

  
    
    YIELD_IPC_ONCRPC_ERROR( ProgramUnavailable, 1, "program unavailable" );
    YIELD_IPC_ONCRPC_ERROR( ProgramMismatch, 2, "program mismatch" );
    YIELD_IPC_ONCRPC_ERROR( ProcedureUnavailable, 3, "procedure unavailable" );
    YIELD_IPC_ONCRPC_ERROR( GarbageArguments, 4, "garbage arguments" );    
    YIELD_IPC_ONCRPC_ERROR( System, 5, "system error" );
    YIELD_IPC_ONCRPC_ERROR( RPCMismatch, 6, "RPC version mismatch" );


    class ONCRPCMessage
    {
    public:
      enum auth_flavor { AUTH_NONE = 0, AUTH_SYS = 1, AUTH_SHORT = 2 };
      enum msg_type { CALL = 0, REPLY = 1 };

    public:
      virtual ~ONCRPCMessage();

      MarshallableObject* get_verf() const { return verf; }
      uint32_t get_xid() const { return xid; }

    protected:
      ONCRPCMessage( YRO_NEW_REF MarshallableObject* verf, uint32_t xid );

      void marshal_opaque_auth( Marshaller&, MarshallableObject* ) const;

    private:
      MarshallableObject* verf;
      uint32_t xid;
    };


    template <class ONCRPCMessageParserType, class ONCRPCMessageType>
    class ONCRPCMessageParser
    {
    protected:
      ONCRPCMessageParser( MessageFactory&, bool parse_records );
      virtual ~ONCRPCMessageParser();

      MessageFactory& get_message_factory() const { return message_factory; }

      bool
      parse
      (
        Buffer& buffer,
        ONCRPCMessageType*& out_onc_rpc_message,
        Buffer** out_next_buffer = NULL
      );

      MarshallableObject* unmarshal_opaque_auth( XDRUnmarshaller& );

    private:
      MessageFactory& message_factory;
      bool parse_records;
    };


    class ONCRPCRequest : public RPCRequest, public ONCRPCMessage
    {
    public:
      ONCRPCRequest
      (
        YRO_NEW_REF Request& body,
        uint32_t prog,
        uint32_t vers,
        uint32_t xid,
        YRO_NEW_REF MarshallableObject* cred = NULL, // = AUTH_NONE
        YRO_NEW_REF MarshallableObject* verf = NULL // = AUTH_NONE
      );

      MarshallableObject* get_cred() const { return get_credentials(); }
      uint32_t get_proc() const { return get_body().get_type_id(); }      
      uint32_t get_prog() const { return prog; }
      uint32_t get_vers() const { return vers; }
      YRO_NEW_REF Buffers& marshal( bool in_record = false ) const;
      void respond( YRO_NEW_REF ONCRPCResponse& response );
      void respond( YRO_NEW_REF Response& response );
      void respond( YRO_NEW_REF Exception& response );

      // Object
      ONCRPCRequest& inc_ref() { return Object::inc_ref( *this ); }

      // MarshallableObject
      const static uint32_t TYPE_ID = 213;
      uint32_t get_type_id() const { return TYPE_ID; }
      const char* get_type_name() const { return "ONCRPCRequest"; }
      void marshal( Marshaller& marshaller ) const;
      void unmarshal( Unmarshaller& unmarshaller ) { }

    private:      
      uint32_t prog, vers;
    };


    class ONCRPCRequestHandler : public RequestHandler
    {
    public:
      virtual void handle( YRO_NEW_REF ONCRPCRequest& onc_rpc_request ) = 0;

      // RequestHandler
      virtual void handle( YRO_NEW_REF Request& request );
    };


    class ONCRPCRequestParser 
      : public ONCRPCMessageParser<ONCRPCRequestParser, Message>
    {
    public:
      ONCRPCRequestParser( MessageFactory&, bool parse_records = false );

      bool
      parse
      (
        Buffer& buffer,        
        Message*& out_onc_rpc_message,
        Buffer** out_next_buffer = NULL
      );

    private:
      template <class, class> friend class ONCRPCMessageParser;
      // ONCRPCMessageParser
      Message* unmarshalONCRPCMessage( XDRUnmarshaller& );
    };


    class ONCRPCResponse : public RPCResponse, public ONCRPCMessage
    {
    public:
      enum accept_stat 
      {
         SUCCESS       = 0,
         PROG_UNAVAIL  = 1,
         PROG_MISMATCH = 2,
         PROC_UNAVAIL  = 3,
         GARBAGE_ARGS  = 4,
         SYSTEM_ERR    = 5
      };

      enum auth_stat 
      {
         AUTH_OK           = 0,
         AUTH_BADCRED      = 1,
         AUTH_REJECTEDCRED = 2,
         AUTH_BADVERF      = 3,
         AUTH_REJECTEDVERF = 4,
         AUTH_TOOWEAK      = 5,
         AUTH_INVALIDRESP  = 6,
         AUTH_FAILED       = 7
      };

      struct mismatch_info
      {
        uint32_t low;
        uint32_t high;
      };

      enum reject_stat { RPC_MISMATCH = 0, AUTH_ERROR = 1 };
      enum reply_stat { MSG_ACCEPTED = 0, MSG_DENIED = 1 };

    public:
      // Accepted reply (MSG_ACCEPTED) - SUCCESS accept_stat
      ONCRPCResponse
      ( 
        YRO_NEW_REF Response& body,
        uint32_t xid,
        YRO_NEW_REF MarshallableObject* verf = NULL // = AUTH_NONE
      );

      // Accepted reply (MSG_ACCEPTED) - other accept_stat = body.get_type_id()
      ONCRPCResponse
      (
        YRO_NEW_REF Exception& body,
        uint32_t xid,
        YRO_NEW_REF MarshallableObject* verf = NULL // = AUTH_NONE
      );

      // Rejected reply (MSG_DENIED) - RPC_MISMATCH
      ONCRPCResponse( const struct mismatch_info&, uint32_t xid );

      // Rejected reply (MSG_DENIED ) - AUTH_ERROR
      ONCRPCResponse( auth_stat, uint32_t xid );

      YRO_NEW_REF Buffers& marshal( bool in_record = false ) const;

      // Object
      ONCRPCResponse& inc_ref() { return Object::inc_ref( *this ); }

      // MarshallableObject
      const static uint32_t TYPE_ID = 214;
      uint32_t get_type_id() const { return TYPE_ID; }
      const char* get_type_name() const { return "ONCRPCResponse"; }
      void marshal( Marshaller& marshaller ) const;
      void unmarshal( Unmarshaller& unmarshaller ) { }

    private:
      uint32_t accept_stat_;
      auth_stat auth_stat_;
      struct mismatch_info mismatch_info_;
      reject_stat reject_stat_;
      reply_stat reply_stat_;
    };


    class ONCRPCResponseParser 
      : public ONCRPCMessageParser<ONCRPCResponseParser, ONCRPCResponse>
    {
    public:
      ONCRPCResponseParser( MessageFactory&, bool parse_records = false );

      bool
      parse
      (
        Buffer& buffer,
        ONCRPCRequest& onc_rpc_request,
        ONCRPCResponse*& out_onc_rpc_response,
        Buffer** out_next_buffer = NULL
      );

    private:
      template <class, class> friend class ONCRPCMessageParser;
      // ONCRPCMessageParser
      ONCRPCResponse* unmarshalONCRPCMessage( XDRUnmarshaller& );

    private:
      ONCRPCRequest* onc_rpc_request;
    };


    class ONCRPCAuthError : public Exception
    {
    public:
      const static uint32_t ERROR_CODE = 7;

      ONCRPCAuthError( ONCRPCResponse::auth_stat auth_stat_ ) 
        : Exception( ERROR_CODE, "ONC-RPC: auth error" ),
          auth_stat_( auth_stat_ )
      { }

      ONCRPCResponse::auth_stat get_auth_stat() const { return auth_stat_; }

    private:
      ONCRPCResponse::auth_stat auth_stat_;
    };


    class ONCRPCClient : public RPCClient<ONCRPCRequest, ONCRPCResponse>
    {
    public:
      virtual ~ONCRPCClient() { }

      uint32_t get_prog() const { return prog; }
      uint32_t get_vers() const { return vers; }

      virtual void handle( YRO_NEW_REF ONCRPCRequest& onc_rpc_request ) = 0;

      // RequestHandler
      virtual void handle( YRO_NEW_REF Request& request );

    protected:
      ONCRPCClient
      (
        YRO_NEW_REF MessageFactory& message_factory,
        uint32_t prog,
        uint32_t vers
      );
    
    private:
      uint32_t prog, vers;
    };


    typedef RPCServer<ONCRPCRequest, ONCRPCResponse> ONCRPCServer;


    template <class StreamSocketType = StreamSocket>
    class ONCRPCStreamSocketClient 
      : public ONCRPCClient,
        public StreamSocketClient<StreamSocketType>
    {
    public:
      typedef typename
        StreamSocketClient<StreamSocketType>::Configuration Configuration;

      ONCRPCStreamSocketClient
      (
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF SocketAddress& peername,
        uint32_t prog,
        YRO_NEW_REF StreamSocketType& stream_socket,
        uint32_t vers,
        YRO_NEW_REF Configuration* configuration = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      virtual ~ONCRPCStreamSocketClient() { }
   
    private:
      // ONCRPCClient
      virtual void handle( YRO_NEW_REF ONCRPCRequest& onc_rpc_request );

    private:
     class Connection 
        : public StreamSocketClient<StreamSocketType>::Connection,
          private ONCRPCResponseParser
      {
      public:
        Connection
        ( 
          ONCRPCStreamSocketClient<StreamSocketType>&,
          YRO_NEW_REF StreamSocketType& stream_socket
        );

        void handle( YRO_NEW_REF ONCRPCRequest& onc_rpc_request );

      private:
        // StreamSocket::AIORecvCallback
        void onReadCompletion( Buffer& buffer, void* context );

        // StreamSocket::AIOSendCallback
        void onWriteCompletion( size_t bytes_sent, void* context );
        void onWriteError( uint32_t error_code, void* context );

        // StreamSocketClient::Connection
        void onError( uint32_t error_code, void* context );

      private:
        typedef typename StreamSocketClient<StreamSocketType>::
          template ConnectionQueue<Connection> ConnectionQueue;
        ConnectionQueue& connection_queue;
        
        map<uint32_t, ONCRPCRequest*> live_onc_rpc_requests;
      };

      typedef typename StreamSocketClient<StreamSocketType>::
         template ConnectionQueue<Connection> ConnectionQueue;
      ConnectionQueue connection_queue;    
    };


    template <class StreamSocketType>
    class ONCRPCStreamSocketServer 
      : private ONCRPCServer, 
        public StreamSocketServer<StreamSocketType>
    {
    public:
      ONCRPCStreamSocketServer
      ( 
        YRO_NEW_REF StreamSocketType& listen_stream_socket,
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF EventHandler& request_handler,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      virtual ~ONCRPCStreamSocketServer() { }

    private:
      // StreamSocketType::AIOAcceptCallback
      void onAcceptCompletion( StreamSocketType&, void*, Buffer* );

    private:
      class Connection;
    };


#ifdef YIELD_PLATFORM_HAVE_OPENSSL
    class ONCRPCSSLSocketClient : public ONCRPCStreamSocketClient<SSLSocket>
    {
    public:
      ONCRPCSSLSocketClient
      (
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF SocketAddress& peername,
        uint32_t prog,
        YRO_NEW_REF SSLSocket& ssl_socket,
        uint32_t vers,
        YRO_NEW_REF Configuration* configuration = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      virtual ~ONCRPCSSLSocketClient() { }

      static ONCRPCSSLSocketClient&
      create
      (
        const URI& absolute_uri,
        YRO_NEW_REF MessageFactory& message_factory,
        uint32_t prog,
        uint32_t vers,
        YRO_NEW_REF Configuration* configuration = NULL,
        Log* error_log = NULL,
        SSLContext* ssl_context = NULL,
        Log* trace_log = NULL
      );
    };


    class ONCRPCSSLSocketServer : public ONCRPCStreamSocketServer<SSLSocket>
    {
    public:
      ONCRPCSSLSocketServer
      ( 
        YRO_NEW_REF SSLSocket& listen_ssl_socket,
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF EventHandler& request_handler,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      static ONCRPCSSLSocketServer&
      create
      (        
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF EventHandler& request_handler,
        const SocketAddress& sockname,
        SSLContext& ssl_context,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );
    };
#endif


    class ONCRPCTCPSocketClient : public ONCRPCStreamSocketClient<TCPSocket>
    {
    public:
      ONCRPCTCPSocketClient
      (
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF SocketAddress& peername,
        uint32_t prog,
        YRO_NEW_REF TCPSocket& tcp_socket,
        uint32_t vers,
        YRO_NEW_REF Configuration* configuration = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      virtual ~ONCRPCTCPSocketClient() { }

      static ONCRPCTCPSocketClient&
      create
      (
        const URI& absolute_uri,
        YRO_NEW_REF MessageFactory& message_factory,
        uint32_t prog,
        uint32_t vers,
        YRO_NEW_REF Configuration* configuration = NULL,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );
    };


    class ONCRPCTCPSocketServer : public ONCRPCStreamSocketServer<TCPSocket>
    {
    public:
      ONCRPCTCPSocketServer
      ( 
        YRO_NEW_REF TCPSocket& listen_tcp_socket,
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF EventHandler& request_handler,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      static ONCRPCTCPSocketServer&
      create
      (
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF EventHandler& request_handler,
        const SocketAddress& sockname,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );
    };


    class ONCRPCUDPSocketClient 
      : public ONCRPCClient,
        public UDPSocketClient,        
        private ONCRPCResponseParser
    {
    public:
      ONCRPCUDPSocketClient
      (
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF SocketAddress& peername,
        uint32_t prog,
        YRO_NEW_REF UDPSocket& udp_socket,
        uint32_t vers,
        Log* error_log = NULL,
        const Time& recv_timeout = RECV_TIMEOUT_DEFAULT,
        Log* trace_log = NULL
      );

      static ONCRPCUDPSocketClient&
      create
      (
        const URI& absolute_uri,
        YRO_NEW_REF MessageFactory& message_factory,
        uint32_t prog,
        uint32_t vers,
        Log* error_log = NULL,        
        const Time& recv_timeout = RECV_TIMEOUT_DEFAULT,
        Log* trace_log = NULL
      );

    protected:
      // ONCRPCClient
      virtual void handle( ONCRPCRequest& onc_rpc_request );

      // UDPSocket::AIORecvCallback
      void onReadCompletion( Buffer& buffer, void* context );
      void onReadError( uint32_t error_code, void* context );
    };


    class ONCRPCUDPSocketServer 
      : private ONCRPCServer,
        public UDPSocketServer,
        private ONCRPCRequestParser
    {
    public:
      ONCRPCUDPSocketServer
      ( 
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF EventHandler& request_handler,
        YRO_NEW_REF UDPSocket& udp_socket,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

      static ONCRPCUDPSocketServer&
      create
      (
        YRO_NEW_REF MessageFactory& message_factory,
        YRO_NEW_REF EventHandler& request_handler,
        const SocketAddress& sockname,
        Log* error_log = NULL,
        Log* trace_log = NULL
      );

    private:
      // UDPSocket::AIORecvFromCallback
      void onRecvFromCompletion( Buffer&, SocketAddress& peername, void* );
      void onRecvFromError( uint32_t error_code, void* context );

    private:
      class ResponseHandler;
    };


    class TracingSocket
    {
    protected:
      TracingSocket( Log&, socket_t );
      virtual ~TracingSocket();

      Log& get_log() const { return log; }

      void set_peername( const SocketAddress& peername );
      void set_sockname( const SocketAddress& sockname );

      YRO_NEW_REF StreamSocket* trace_accept( YRO_NEW_REF StreamSocket* );
      bool trace_bind( const SocketAddress& sockname, bool ret );
      bool trace_connect( const SocketAddress& peername, bool ret );
      ssize_t trace_recv( void* buf, size_t buflen, ssize_t ret );
      ssize_t trace_send( const void* buf, size_t buflen, ssize_t ret );
      ssize_t trace_sendmsg( const struct iovec*, uint32_t iovlen, ssize_t ret );

    private:
      Log& log;
      socket_t socket_;
      string peername, sockname;
    };

#define YIELD_IPC_TRACING_SOCKET_PROTOTYPES\
      bool bind( const SocketAddress& to_sockaddr );\
      bool connect( const SocketAddress& peername );\
      YIELD_PLATFORM_SOCKET_RECV_SEND_PROTOTYPES;


#ifdef YIELD_PLATFORM_HAVE_OPENSSL
    class TracingSSLSocket : public SSLSocket, private TracingSocket
    {
    public:
      static YRO_NEW_REF TracingSSLSocket* create( Log&, SSLContext& );

      static YRO_NEW_REF TracingSSLSocket*
      create
      ( 
        int domain,
        Log&,
        SSLContext&
      );

      // Socket
      YIELD_IPC_TRACING_SOCKET_PROTOTYPES;

      // StreamSocket
      YRO_NEW_REF StreamSocket* accept();

      // SSLSocket
      virtual YRO_NEW_REF SSLSocket* dup();

    private:
      TracingSSLSocket( int domain, Log&, socket_t, SSL*, SSLContext& );

      // StreamSocket
      YRO_NEW_REF StreamSocket* dup2( socket_t );
    };
#endif


    class TracingTCPSocket : public TCPSocket, private TracingSocket
    {
    public:
      static YRO_NEW_REF TracingTCPSocket* create( Log& log );
      static YRO_NEW_REF TracingTCPSocket* create( int domain, Log& log );

      // Socket      
      YIELD_IPC_TRACING_SOCKET_PROTOTYPES;

      // StreamSocket
      YRO_NEW_REF StreamSocket* accept();

      // TCPSocket
      virtual YRO_NEW_REF TCPSocket* dup();

    private:
      TracingTCPSocket( int domain, Log&, socket_t );

      // StreamSocket
      StreamSocket* dup2( socket_t );
    };


    class TracingUDPSocket : public UDPSocket, private TracingSocket
    {
    public:
      static YRO_NEW_REF TracingUDPSocket* create( Log& log );
      static YRO_NEW_REF TracingUDPSocket* create( int domain, Log& log );

      // Socket
      YIELD_IPC_TRACING_SOCKET_PROTOTYPES;

      // UDPSocket
      ssize_t 
      recvfrom
      (
        void* buf,
        size_t len,
        struct sockaddr_storage& peername,
        int flags = 0 
      );

      ssize_t
      sendmsg
      (
        const struct iovec* iov,
        uint32_t iovlen,
        const SocketAddress& peername,
        int flags = 0
      );

      ssize_t
      sendto
      ( 
        const void* buf,
        size_t len,
        const SocketAddress& peername,
        int flags = 0 
      );

    private:
      TracingUDPSocket( int domain, Log&, socket_t );
    };


    class URI : public Object
    {
    public:
      URI();
      URI( const char* scheme, const char* host, uint16_t port );
      URI( const char* scheme, const char* host, uint16_t port, const char* resource );
      URI( const URI& other );
      // Parsing constructors throw exceptions
      URI( const char* uri );
      URI( const string& uri );
      URI( const char* uri, size_t uri_len );
      virtual ~URI() { }

      const string& get_scheme() const { return scheme; }
      const string& get_host() const { return host; }
      const string& get_user() const { return user; }
      const string& get_password() const { return password; }
      unsigned short get_port() const { return port; }
      const string& get_resource() const { return resource; }
      const multimap<string, string>& get_query() const { return query; }

      string get_query_value
      (
        const string& key,
        const char* default_value = ""
      ) const;

      multimap<string, string>::const_iterator
      get_query_values
      ( 
        const string& key 
      ) const;

      URI& operator=( const URI& );
      operator string() const;
      operator YRO_NEW_REF SocketAddress*() const;
      operator YRO_NEW_REF SocketAddress&() const; // throws exceptions

      static YRO_NEW_REF URI* parse( const char* uri );
      static YRO_NEW_REF URI* parse( const string& uri );
      static YRO_NEW_REF URI* parse( const char* uri, size_t uri_len );

      void set_port( uint16_t port ) { this->port = port; }

    private:
      URI( UriUriStructA& parsed_uri );

      void init( const char* uri, size_t uri_len );
      void init( UriUriStructA& parsed_uri );

    private:
      string scheme, user, password, host;
      unsigned short port;
      string resource;
      multimap<string, string> query;
    };
  };
};

#endif
