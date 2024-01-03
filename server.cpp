#include <sys/socket.h> // For socket functions
#include <netinet/in.h> // For sockaddr_in
#include <cstdlib> // For exit() and EXIT_FAILURE
#include <iostream> // For cout
#include <sstream>
#include <vector>
#include <unistd.h> // For read
#include <mariadb/mysql.h>
#include <cstring>
#include <iomanip>

using namespace std;

const char *dbserver = "localhost";
const char *dbusername = "lewis";
const char *dbpassword = "password";
const char *dbdatabase = "wordelites";

MYSQL mysql, *conn;
MYSQL_RES *res;
MYSQL_ROW row;


int signup(vector<string> v, sockaddr_in* sockaddr) {

	
	std::string query;
	int query_state;

	
	conn = mysql_real_connect(&mysql, dbserver, dbusername, dbpassword, dbdatabase, 0, 0, 0);
	if (conn == NULL)
	{
		cout << mysql_error(&mysql) << endl << endl;
		return 1;
	}

	string username = v[1];
	string password = v[2];
	string ip = to_string(sockaddr->sin_addr.s_addr);

	query = "SELECT * FROM users WHERE username = '" + username + "'";
	query_state = mysql_query(conn, query.c_str());

	if (query_state != 0)
	{
		cout << mysql_error(conn) << endl << endl;
		return 1;
	}
	res = mysql_store_result(conn);

	if ((row = mysql_fetch_row(res)) == NULL)
	{
		query = "INSERT INTO users (username, rating, nickname, lastip, lastonline, password) VALUES ('" + username + "', 100, '', '" + ip + "', 0, '" + password + "')";
		query_state = mysql_query(conn, query.c_str());

		if (query_state != 0)
		{
			cout << mysql_error(conn) << endl << endl;
			return 1;
		}
		//cout << left;
		//cout << setw(18) << row[0]
		//	<< setw(18) << row[1]
		//	<< setw(18) << row[2]
		//	<< setw(18) << row[3]
		//	<< setw(18) << row[4]
		//	<< setw(18) << row[5]
		//	<< setw(18) << row[6] << endl;
	}

	mysql_free_result(res);
	mysql_close(conn);
}

int main() {

	mysql_init(&mysql);

	// Create a socket (IPv4, TCP)
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		std::cout << "Failed to create socket. errno: " << errno << std::endl;
		exit(EXIT_FAILURE);
	}

	// Listen to port 9999 on any address
	sockaddr_in sockaddr;
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_addr.s_addr = INADDR_ANY;
	sockaddr.sin_port = htons(80); // htons is necessary to convert a number to
									 // network byte order


	  //listen to all destination addresses on the incoming packet and on port 9999
	 //it sets _our_ address , and all packets destined to to it will be accepted
	  //and all packets sent from us will have this address and port
	  //effectively it means that it accepts all communications to port 9999 regardless of address
	  //and sends all packets from the appropriate outgoing address and port 9999

	if (bind(sockfd, (struct sockaddr*)&sockaddr, sizeof(sockaddr)) < 0) {
		std::cout << "Failed to bind to port 80. errno: " << errno << std::endl;
		exit(EXIT_FAILURE);
	}


	std::cout << "listening " << errno << std::endl;
	// Start listening. Hold at most 10 connections in the queue
	if (listen(sockfd, 10) < 0) {
		std::cout << "Failed to listen on socket. errno: " << errno << std::endl;
		exit(EXIT_FAILURE);
	}
	std::cout << "done " << errno << std::endl;
	// Grab a connection from the queue
	auto addrlen = sizeof(sockaddr);
	int connection = 0;
	while ((connection = accept(sockfd, (struct sockaddr*)&sockaddr, (socklen_t*)&addrlen)) >= 0) {
		if (connection < 0) {
			std::cout << "Failed to grab connection. errno: " << errno << std::endl;
			exit(EXIT_FAILURE);
		}

		// Read from the connection
		char buffer[100];
		auto bytesRead = read(connection, buffer, 100);
		
		std::cout << "The message was: " << buffer << endl;

		string str(buffer);
		vector<string> v;

		stringstream ss(str);

		while (ss.good()) {
			string substr;
			getline(ss, substr, ',');
			v.push_back(substr);
		}


		if (memcmp(buffer, "signup", 6) == 0) {
			for (size_t i = 0; i < v.size(); i++)
				cout << v[i] << endl;

			signup(v, (sockaddr_in*)&sockaddr);
		}

		std::string response = "Good talking to you\n";
		send(connection, response.c_str(), response.size(), 0);
	}

	// Close the connections
	close(connection);
	close(sockfd);
}
