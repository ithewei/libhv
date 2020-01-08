// @see poco/Net/samples/EchoServer

#include "Poco/Net/SocketReactor.h"
#include "Poco/Net/SocketAcceptor.h"
#include "Poco/Net/SocketNotification.h"
#include "Poco/Net/StreamSocket.h"
#include "Poco/Net/ServerSocket.h"
#include "Poco/NObserver.h"
#include "Poco/Exception.h"
#include "Poco/Thread.h"
#include "Poco/FIFOBuffer.h"
#include "Poco/Delegate.h"
#include "Poco/Util/ServerApplication.h"
#include "Poco/Util/Option.h"
#include "Poco/Util/OptionSet.h"
#include "Poco/Util/HelpFormatter.h"
#include <iostream>

using Poco::Net::SocketReactor;
using Poco::Net::SocketAcceptor;
using Poco::Net::ReadableNotification;
using Poco::Net::WritableNotification;
using Poco::Net::ShutdownNotification;
using Poco::Net::ServerSocket;
using Poco::Net::StreamSocket;
using Poco::NObserver;
using Poco::AutoPtr;
using Poco::Thread;
using Poco::FIFOBuffer;
using Poco::delegate;
using Poco::Util::ServerApplication;
using Poco::Util::Application;
using Poco::Util::Option;
using Poco::Util::OptionSet;
using Poco::Util::HelpFormatter;

class EchoServiceHandler
	/// I/O handler class. This class (un)registers handlers for I/O based on
	/// data availability. To ensure non-blocking behavior and alleviate spurious
	/// socket writability callback triggering when no data to be sent is available,
	/// FIFO buffers are used. I/O FIFOBuffer sends notifications on transitions
	/// from [1] non-readable (i.e. empty) to readable, [2] writable to non-writable 
	/// (i.e. full) and [3] non-writable (i.e. full) to writable.
	/// Based on these notifications, the handler member functions react by
	/// enabling/disabling respective reactor framework notifications.
{
public:
	EchoServiceHandler(StreamSocket& socket, SocketReactor& reactor):
		_socket(socket),
		_reactor(reactor),
		_fifoIn(BUFFER_SIZE, true),
		_fifoOut(BUFFER_SIZE, true)
	{
		_reactor.addEventHandler(_socket, NObserver<EchoServiceHandler, ReadableNotification>(*this, &EchoServiceHandler::onSocketReadable));
		_reactor.addEventHandler(_socket, NObserver<EchoServiceHandler, ShutdownNotification>(*this, &EchoServiceHandler::onSocketShutdown));

		_fifoOut.readable += delegate(this, &EchoServiceHandler::onFIFOOutReadable);
		_fifoIn.writable += delegate(this, &EchoServiceHandler::onFIFOInWritable);
	}
	
	~EchoServiceHandler()
	{
		_reactor.removeEventHandler(_socket, NObserver<EchoServiceHandler, ReadableNotification>(*this, &EchoServiceHandler::onSocketReadable));
		_reactor.removeEventHandler(_socket, NObserver<EchoServiceHandler, WritableNotification>(*this, &EchoServiceHandler::onSocketWritable));
		_reactor.removeEventHandler(_socket, NObserver<EchoServiceHandler, ShutdownNotification>(*this, &EchoServiceHandler::onSocketShutdown));

		_fifoOut.readable -= delegate(this, &EchoServiceHandler::onFIFOOutReadable);
		_fifoIn.writable -= delegate(this, &EchoServiceHandler::onFIFOInWritable);
	}
	
	void onFIFOOutReadable(bool& b)
	{
		if (b)
			_reactor.addEventHandler(_socket, NObserver<EchoServiceHandler, WritableNotification>(*this, &EchoServiceHandler::onSocketWritable));
		else
			_reactor.removeEventHandler(_socket, NObserver<EchoServiceHandler, WritableNotification>(*this, &EchoServiceHandler::onSocketWritable));
	}
	
	void onFIFOInWritable(bool& b)
	{
		if (b)
			_reactor.addEventHandler(_socket, NObserver<EchoServiceHandler, ReadableNotification>(*this, &EchoServiceHandler::onSocketReadable));
		else
			_reactor.removeEventHandler(_socket, NObserver<EchoServiceHandler, ReadableNotification>(*this, &EchoServiceHandler::onSocketReadable));
	}
	
	void onSocketReadable(const AutoPtr<ReadableNotification>& pNf)
	{
		try
		{
			int len = _socket.receiveBytes(_fifoIn);
			if (len > 0)
			{
				_fifoIn.drain(_fifoOut.write(_fifoIn.buffer(), _fifoIn.used()));
			}
			else
			{
				delete this;
			}
		}
		catch (Poco::Exception& exc)
		{
			Application& app = Application::instance();
			app.logger().log(exc);
			delete this;
		}
	}
	
	void onSocketWritable(const AutoPtr<WritableNotification>& pNf)
	{
		try
		{
			_socket.sendBytes(_fifoOut);
		}
		catch (Poco::Exception& exc)
		{
			Application& app = Application::instance();
			app.logger().log(exc);
			delete this;
		}
	}

	void onSocketShutdown(const AutoPtr<ShutdownNotification>& pNf)
	{
		delete this;
	}
	
private:
	enum
	{
		BUFFER_SIZE = 1024
	};
	
	StreamSocket   _socket;
	SocketReactor& _reactor;
	FIFOBuffer     _fifoIn;
	FIFOBuffer     _fifoOut;
};


class EchoServer: public Poco::Util::ServerApplication
{
public:
	EchoServer()
	{
	}
	
	~EchoServer()
	{
	}

protected:
	void initialize(Application& self)
	{
		ServerApplication::initialize(self);
	}
		
	void uninitialize()
	{
		ServerApplication::uninitialize();
	}

	int main(const std::vector<std::string>& args)
	{
        if (args.size() < 1) {
            printf("Usage: cmd port\n");
            return -10;
        }
        int port = atoi(args[0].c_str());
        // set-up a server socket
        ServerSocket svs(port);
        // set-up a SocketReactor...
        SocketReactor reactor;
        // ... and a SocketAcceptor
        SocketAcceptor<EchoServiceHandler> acceptor(svs, reactor);
        // run the reactor in its own thread so that we can wait for 
        // a termination request
        Thread thread;
        thread.start(reactor);
        // wait for CTRL-C or kill
        waitForTerminationRequest();
        // Stop the SocketReactor
        reactor.stop();
        thread.join();
        return Application::EXIT_OK;
	}
};

int main(int argc, char** argv)
{
	EchoServer app;
	return app.run(argc, argv);
}
