#include <filesystem>
#include <chrono>
#include "websocket_client.h"

const int REPEAT_NUM = 3;

WebSocketClient::WebSocketClient(std::shared_ptr<Allxon::Octo> octo)
    : octo_(octo), received_keyword_(""), alert_enabled_(false), alert_trigger_(false), enableSaveFile_(false), 
    outputDirectory_(""), state_serial_port_(""), state_output_file_(""), state_trigger_(false)
{
    endpoint_.set_reuse_addr(true);
    endpoint_.clear_access_channels(websocketpp::log::alevel::all);
    endpoint_.clear_access_channels(websocketpp::log::elevel::all);
    endpoint_.clear_error_channels(websocketpp::log::alevel::all);
    endpoint_.clear_error_channels(websocketpp::log::elevel::all);
    endpoint_.set_tls_init_handler(bind(&WebSocketClient::on_tls_init, this, std::placeholders::_1));
    endpoint_.set_open_handler(bind(&WebSocketClient::on_open, this, std::placeholders::_1));
    endpoint_.set_fail_handler(bind(&WebSocketClient::on_fail, this, std::placeholders::_1));
    endpoint_.set_message_handler(bind(&WebSocketClient::on_message, this, std::placeholders::_1, std::placeholders::_2));
    endpoint_.set_close_handler(bind(&WebSocketClient::on_close, this, std::placeholders::_1));
    endpoint_.init_asio();
    endpoint_.start_perpetual();
    run_thread_.reset(new std::thread(&client::run, &endpoint_));
}
WebSocketClient::~WebSocketClient()
{
}

void WebSocketClient::connect(const std::string &url)
{
    websocketpp::lib::error_code ec;
    client::connection_ptr con = endpoint_.get_connection(url, ec);
    if (ec)
    {
        std::cout << "Connect initialization error: " << ec.message() << std::endl;
        return;
    }
    hdl_ = con->get_handle();
    endpoint_.connect(con);
}

void WebSocketClient::startSerialThread()
{
    serialThreadObj_ = std::thread(&WebSocketClient::serialThread, this);
}

void WebSocketClient::stopSerialThread()
{
    if (serialThreadObj_.joinable())
        serialThreadObj_.join();
}

bool checkTargetString(const std::string &line, const std::string &target)
{
    std::string pattern = "Detecting result: " + target;
    
    if (target.empty())
    	return false;
    
    if (line.find(pattern) != std::string::npos)
        return true;
    
    return false;
}

void WebSocketClient::serialThread()
{
    std::ifstream serial("/dev/ttyACM0");

    if (!serial)
	{
        std::cout << "Failed to open serial port." << std::endl;
		set_state_serial_port("Failed");
		set_state_trigger(true);
        return;
    }

	set_state_serial_port("Open");
	set_state_trigger(true);
    
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y%m%d");
    std::string date = ss.str();
    
    bool closeOutput = false;
    bool saveFileEnabled;
    std::string saveFileDirectory;
    std::string previousSaveFileDirectory;
    std::string filePath;
    std::string receivedKeyword;
    int count = 0;

    std::string line;
    while (std::getline(serial, line))
	{
    	
    	{
    		std::lock_guard<std::mutex> lock(mutex_);
    		receivedKeyword = received_keyword_;
    	}
    	
    	if (checkTargetString(line, receivedKeyword))
		{
            count++;
        }

		if (count == REPEAT_NUM)
		{
			// set_alert_trigger(true);
			std::cout << receivedKeyword << " has appeared 3 times in row." << std::endl;
			count = 0;
		}
        
        {    
		    std::lock_guard<std::mutex> lock(mutex_);
		    saveFileEnabled = enableSaveFile_;
		    saveFileDirectory = outputDirectory_;
		}
		
		
        if (saveFileEnabled)
		{
        
        	if (saveFileDirectory.empty())
			{
                filePath = Util::plugin_install_dir + "/" + date + "_log.txt";
            }
			else
			{
                filePath = saveFileDirectory + "/" + date + "_log.txt";
            }
        	
        	if (saveFileDirectory != previousSaveFileDirectory)
			{
                if (outputFile_.is_open())
				{
                    outputFile_.close();
                    closeOutput = false;
                }

                previousSaveFileDirectory = saveFileDirectory;
            }
        	
        	if (!outputFile_.is_open())
			{
                outputFile_.open(filePath);
                if (!outputFile_)
				{
                    std::cout << "Failed to open output file: " << filePath << std::endl;
					set_state_output_file("Failed");
					set_state_trigger(true);
                }
				else
				{
                    closeOutput = true;
					set_state_output_file("Open");
					set_state_trigger(true);
                }
            }
            
            if (outputFile_.is_open())
			{
                outputFile_ << line << std::endl;
            }
		}
		else if (closeOutput)
		{
		    outputFile_.close();
		    closeOutput = false;
			set_state_output_file("");
			set_state_trigger(true);
		}
    }

    serial.close();
}

void WebSocketClient::run()
{
    connect(octo_->get_websocket_url());
    int count = 0;
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        send_np_command_ack(cmd_accept_queue_);
        send_np_command_ack(cmd_ack_queue_);	
        
        if (state_trigger())
        {
            send_np_states_metrics();
            set_state_trigger(false);
        }
        
		if (alert_trigger())
        {
            if (is_alert_enabled())
                send_np_alert(); 
            set_alert_trigger(false);
        }
    }
}
context_ptr WebSocketClient::on_tls_init(websocketpp::connection_hdl hdl)
{
    std::cout << "on_tls_init" << std::endl;
    context_ptr ctx = websocketpp::lib::make_shared<asio::ssl::context>(asio::ssl::context::tlsv12);

    ctx->set_options(asio::ssl::context::default_workarounds |
                     asio::ssl::context::no_sslv2 |
                     asio::ssl::context::no_sslv3 |
                     asio::ssl::context::single_dh_use);

    ctx->set_verify_mode(asio::ssl::verify_none);
    return ctx;
}

void WebSocketClient::on_open(websocketpp::connection_hdl hdl)
{
    std::cout << "on_open" << std::endl;
    send_np_update();
}
void WebSocketClient::on_close(websocketpp::connection_hdl hdl)
{
    std::cout << "on_close" << std::endl;
    exit(0);
}
void WebSocketClient::on_fail(websocketpp::connection_hdl hdl)
{
    std::cout << "on_fail" << std::endl;
    endpoint_.get_alog().write(websocketpp::log::alevel::app, "Connection Failed");
}
void WebSocketClient::on_message(websocketpp::connection_hdl hdl, client::message_ptr msg)
{
    const auto payload = AJson::create(msg->get_payload());
    std::cout << "on_message" << std::endl;
    std::cout << payload.print(false) << std::endl;

    // Handle JSON-RPC error object
    if (payload.has_object_item("error"))
    {
        auto error = payload.item("error");
        printf("Received JSON-RPC error object, error code: %s, error_message: %s\n",
               error.item("code").string().c_str(), error.item("message").string().c_str());
        return;
    }

    // Verify payload
    std::string get_method;
    if (!octo_->verify(payload.print(false), get_method))
    {
        std::cout << octo_->error_message() << std::endl;
        printf("Received data verify failed: %s\n", octo_->error_message().c_str());
        return;
    }

    std::cout << "Get Method:" << get_method << std::endl;
    if (get_method == "v2/notifyPluginCommand")
    {
        auto cmd_id = payload.item("params").item("commandId").string();
        std::cout << "get command id: " << cmd_id << std::endl;
        set_received_keyword(payload.item("params").item("commands").item(0).item("params").item(0).item("value").string());

        auto cmd_accept = AJson::create(Util::getJsonFromFile(Util::plugin_install_dir + "/plugin_command_ack.json"));
        cmd_accept["params"]["commandId"].set_string(cmd_id);
        cmd_accept["params"]["commandState"].set_string("ACCEPTED");
        cmd_accept["params"]["commandAcks"][0]["result"].add_item_to_object("response", AJson(std::string("Keyword: " + received_keyword()).c_str()));

        auto np_state = AJson::create(Util::getJsonFromFile(Util::plugin_install_dir + "/plugin_state.json"));
        np_state["params"]["states"][0]["value"].set_string(std::string(received_keyword()).c_str());
        np_state["params"]["states"][1]["value"].set_string(std::string(get_state_serial_port()).c_str());
        np_state["params"]["states"][2]["value"].set_string(std::string(get_state_output_file()).c_str());
        cmd_accept["params"].add_item_to_object("states", np_state.item("params").item("states"));

        push_command_queue(cmd_accept_queue_, cmd_accept.print(false));

        cmd_accept["params"]["commandState"].set_string("ACKED");
        push_command_queue(cmd_ack_queue_, cmd_accept.print(false));
    }
    else if (get_method == "v2/notifyPluginAlarmUpdate")
    {
        if (!payload.item("params").has_object_item("modules"))
        {
            set_alert_enabled(false);
            return;
        }
        set_alert_enabled(payload.item("params").item("modules").item(0).item("alarms").item(0).item("enabled").boolean());
    }
    else if (get_method == "v2/notifyPluginConfigUpdate")
    {
    	bool saveFile = payload.item("params").item("modules").item(1).item("configs").item(0).item("params").item(0).item("value").string() == "onValue" ? true : false;
    	set_savefile_enabled(saveFile);
    	
    	if (saveFile)
    	{
    		std::string directory = payload.item("params").item("modules").item(1).item("configs").item(1).item("params").item(0).item("value").string();
    		set_savefile_directory(directory);
    	}
    }
}
void WebSocketClient::send_np_update()
{
    std::cout << "send_np_update" << std::endl;
    auto np_update = AJson::create(Util::getJsonFromFile(Util::plugin_install_dir + "/plugin_update_template.json"));
    np_update["params"]["modules"][0]["properties"][0]["value"].set_string(std::filesystem::canonical(Util::plugin_install_dir).string());
    verify_and_send(np_update.print(false));
}

void WebSocketClient::send_np_states_metrics()
{
    std::cout << "SendPluginStateMetrics" << std::endl;
    auto np_state = AJson::create(Util::getJsonFromFile(Util::plugin_install_dir + "/plugin_state.json"));
    np_state["params"]["states"][0]["value"].set_string(received_keyword());
    np_state["params"]["states"][1]["value"].set_string(get_state_serial_port());
	np_state["params"]["states"][2]["value"].set_string(get_state_output_file());
    verify_and_send(np_state.print(false));
}

void WebSocketClient::send_np_command_ack(std::queue<std::string> &queue)
{
    if (queue.empty())
        return;
    std::cout << "send_np_command_ack" << std::endl;
    std::string np_cmd_ack_str;
    while (pop_command_queue(queue, np_cmd_ack_str) && verify_and_send(np_cmd_ack_str))
    {
    }
}

void WebSocketClient::send_np_alert()
{
    std::cout << "send_np_alert" << std::endl;
    auto np_alert = AJson::create(Util::getJsonFromFile(Util::plugin_install_dir + "/plugin_alert.json"));
    np_alert["params"]["alarms"][0]["message"].set_string(received_keyword() + " has appeared 3 times in a row.");
    np_alert["params"]["alarms"][0]["time"].set_string(std::to_string(time(NULL)));
    verify_and_send(np_alert.print(false));
}

bool WebSocketClient::verify_and_send(const std::string &json)
{
    auto send_json = json;
    if (!octo_->sign(send_json))
    {
        std::cout << octo_->error_message().c_str() << std::endl;
        set_alert_trigger(false);
        return false;
    }

    endpoint_.send(hdl_, send_json.c_str(), websocketpp::frame::opcode::TEXT);
    std::cout << "Send:" << send_json << std::endl;
    return true;
}

void WebSocketClient::push_command_queue(std::queue<std::string> &queue, std::string data)
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue.push(data);
}

bool WebSocketClient::pop_command_queue(std::queue<std::string> &queue, std::string &pop_data)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue.empty())
        return false;
    pop_data = queue.front();
    queue.pop();
    return true;
}

void WebSocketClient::set_alert_enabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    alert_enabled_ = enabled;
}

bool WebSocketClient::is_alert_enabled() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return alert_enabled_;
}

void WebSocketClient::set_received_keyword(const std::string &keyword)
{
    std::lock_guard<std::mutex> lock(mutex_);
    received_keyword_ = keyword;
}

std::string WebSocketClient::received_keyword() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return received_keyword_;
}

void WebSocketClient::set_alert_trigger(bool need_trigger)
{
    std::lock_guard<std::mutex> lock(mutex_);
    alert_trigger_ = need_trigger;
}

bool WebSocketClient::alert_trigger() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return alert_trigger_;
}

void WebSocketClient::set_savefile_enabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    enableSaveFile_ = enabled;
}

bool WebSocketClient::is_savefile_enabled() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return enableSaveFile_;
}

void WebSocketClient::set_savefile_directory(const std::string &dir)
{
    std::lock_guard<std::mutex> lock(mutex_);
    outputDirectory_ = dir;
}

std::string WebSocketClient::get_savefile_directory() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return outputDirectory_;
}

void WebSocketClient::set_state_serial_port(const std::string &state)
{
	std::lock_guard<std::mutex> lock(mutex_);
	state_serial_port_ = state;
}

std::string WebSocketClient::get_state_serial_port() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return state_serial_port_;
}

void WebSocketClient::set_state_output_file(const std::string &state)
{
	std::lock_guard<std::mutex> lock(mutex_);
	state_output_file_ = state;
}

std::string WebSocketClient::get_state_output_file() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return state_output_file_;
}

void WebSocketClient::set_state_trigger(bool need_trigger)
{
    std::lock_guard<std::mutex> lock(mutex_);
    state_trigger_ = need_trigger;
}

bool WebSocketClient::state_trigger() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_trigger_;
}
