#ifndef TCP
#define TCP
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <map>
#include <cstdio>
#include <strings.h>
#include <csignal>
#include <thread>
#include <vector>

bool samestr(std::string str0, std::string str1){
	for (int i = 0; i < str0.length(); i++){
		if (str0[i]!=str1[i])return false;
	}
	return true;
}

struct connectionlist_node
{
	int sock;
	sockaddr_in addr;
	int port; //we set again here because ntohs(addr.sin_port) after transfer data will return dynamic port
};

sockaddr_in generate_addr(char *ip, int port, int AF)
{
	sockaddr_in toreturn; //structure addr
	inet_pton(AF, ip, &toreturn.sin_addr);
	toreturn.sin_port = htons(port);
	toreturn.sin_family = AF;
	return toreturn;
}

//clientid, address
typedef void(*confunc)(int, sockaddr_in);

class TCP
{
public:
	//we use static because if the methods executed with another thread, those attributes will become new again
	static std::map<int, struct connectionlist_node> connections;
	static std::map<int, bool> listening;
	static int unused_id;
	
	int get_sock()
	{
		int to_return = socket(AF_INET, SOCK_STREAM, 0);
		int one = 1;
		setsockopt(to_return, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(to_return));
		return to_return;
	}
	
	TCP()
	{}
	
	int register_connection(int sock, sockaddr_in addr, int listen=0, int port = -1)
	{
		int theport = (port <= 0) ? ntohs(addr.sin_port): port;
		struct connectionlist_node tcp = (struct connectionlist_node){.sock=sock, .addr=addr, .port = theport};
		connections[unused_id] = tcp;
		if (listen)
		{
			listening[unused_id]=true;
		}
		unused_id++;
		return unused_id-1;
	}

	char* receive(int id, int buffersize=1024, char ender=EOF, int closeafterrecv=0)
	{
		//printf("start receive()\n");
		char *to_return = (char*)calloc(sizeof(char), 1);
		int *sock = &connections[id].sock;
		if (buffersize==0){
			char recv[1];
			int index = 0;
			do{
				bzero(&recv, sizeof(recv));
				read(*sock, &recv, 1);
				to_return[index] = recv[0];
				index++;
				to_return = (char*)realloc(to_return, sizeof(char)*(index+1));
				printf("%d\n", recv[0]);
			}while(recv[0]!=ender);
			if (closeafterrecv)
			{
				close(*sock);
			}
			//printf("end flexible receive()\n");
			return to_return;
		}
		else{
			char *to_returns = (char*)calloc(sizeof(char), buffersize);
			read(connections[id].sock, to_returns, buffersize);
			if (closeafterrecv)
			{
				close(*sock);
			}
			//printf("end unflexible receive()\n");
			return to_returns;
		}
	}

	int send(sockaddr_in addr, char* tosend, int size, int closeaftersend = 1, int newconnection = 1)
	{
		//printf("start send()\n");
		int found = 0;
		char ip[32];
		inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(addr));
		int port = ntohs(addr.sin_port);
		if (!newconnection){
			for (int i = 0; i < unused_id; i ++)
			{
				sockaddr_in temp = this->connections[i].addr;
				char ipnya[32];
				inet_ntop(AF_INET, &temp.sin_addr, ipnya, sizeof(ipnya));
				if (samestr(ip, ipnya))
				{
					if ((write(connections[i].sock, tosend, size))<0)
					{
						printf("send: cannot write\n");
					}
					found = 1;
					if (closeaftersend)
					{
						close(connections[i].sock);
					}
					//printf("end existing send()\n");
					return i; // return existing id
				}
			}
		}

		if (newconnection|(!found)){
			int sockk = this->get_sock();
			int theid = register_connection(sockk, addr, 0);
			int *sock = &connections[theid].sock;
			
			if (connect(*sock, (sockaddr*)&addr, sizeof(addr))<0)
			{
				printf("Cannot connect to %s:%d  send()\n", ip, port);
			}
			
			if ((write(*sock, tosend, size)) < 0)
			{
				printf("send() cannot write buffer\n");
			}
			if (closeaftersend)
			{
				close(*sock);
			}
			//printf("end new send()\n");
			return theid; //return new id
		}
		
	}

	int listenable(int port, const char ip[]="0.0.0.0")
	{
	 sockaddr_in server_test = generate_addr((char *)ip, port, AF_INET);
	 int sock_test = get_sock();
	 if (bind(sock_test, (sockaddr*)&server_test, sizeof(server_test)) < 0)
	 {
	 	return -1;
	 }
	 if (listen(sock_test, 1) < 0)
	 {
	 	return -2;
	 }
	 close(sock_test);
	 return 0;
	}
	void send_to_id(int id,char *to_send, int size,int sambung=1 ,int closeaftersend=1)
	{ 
		//send using socket from connection id

		//printf("start send_to_id()\n");
		sockaddr_in address = connections[id].addr;
		char ip[32];
		int port = ntohs(address.sin_port);
		inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip));
		int &sock = connections[id].sock;

		if (sambung)
		{
			if (connect(sock, (sockaddr*)&address, sizeof(address))<0)
			{
				printf("Cannot connect to %s:%d send_to_id\n", ip, port);
			}
		}

		if ((write(sock, to_send, size))<0)
		{
			printf("send_to_id() cannot write\n");
		}

		if (closeaftersend)
		{
			close(sock);	
		}
		//printf("end send_to_id()\n");
	}

	//the start_listen name is different because std::thread is confuse, which to start
	template <class vfuncp, class obj>
	void start_listen_thread(int port, std::vector<vfuncp> allfunc, obj* objptr, int *return_id)
	{
		char ip[] = "0.0.0.0";
		sockaddr_in server_addr;
		int sock = get_sock();
		bzero(&server_addr, sizeof(server_addr));
		inet_pton(AF_INET, ip, &server_addr.sin_addr);
		server_addr.sin_port = htons(port);
		int bind_result = bind(sock, (sockaddr*)&server_addr, sizeof(server_addr));
		if (bind_result < 0)
		{
			printf("Error. cannot bind on %s:%d\n", ip, port);
			(*return_id) = -1;
			return;
		}
		if (listen(sock, 5)<0)
		{
			printf("Error, cannot listen on %s:%d\n", ip, port);
			(*return_id) = -1;
			return;
		}
		int id = register_connection(sock, server_addr, 1, port);
		(*return_id) = id;
		while (listening[id])
		{
			sockaddr_in client_addr;
			socklen_t clientlen = sizeof(client_addr);
			int newsock = accept(sock, (sockaddr*)&client_addr, &clientlen);
			if (!(listening[id]))
			{
				close(connections[id].sock);
				break;
				return;
			}
			int clientid = register_connection(newsock, client_addr, 0, port);

			for (int i = 0; i < allfunc.size(); i++){
				//(*allfunc[i])(clientid, client_addr);
				std::thread torun = std::thread(allfunc[i], objptr, clientid, client_addr);
				torun.join();
			}
		}
		close(connections[id].sock);
	}
	template <class vfuncp>
	void start_listen_vector(int port, std::vector<vfuncp> allfunc, int *return_id)
	{
		char ip[] = "0.0.0.0";
		sockaddr_in server_addr;
		int sock = get_sock();
		bzero(&server_addr, sizeof(server_addr));
		inet_pton(AF_INET, ip, &server_addr.sin_addr);
		server_addr.sin_port = htons(port);
		int bind_result = bind(sock, (sockaddr*)&server_addr, sizeof(server_addr));
		if (bind_result < 0)
		{
			printf("Error. cannot bind on %s:%d\n", ip, port);
			(*return_id) = -1;
			return;
		}
		if (listen(sock, 5)<0)
		{
			printf("Error, cannot listen on %s:%d\n", ip, port);
			(*return_id) = -1;
			return;
		}
		int id = register_connection(sock, server_addr, 1, port);
		(*return_id) = id;
		while (listening[id])
		{
			sockaddr_in client_addr;
			socklen_t clientlen = sizeof(client_addr);
			int newsock = accept(sock, (sockaddr*)&client_addr, &clientlen);
			if (!(listening[id]))
			{
				close(connections[id].sock);
				break;
				return;
			}
			int clientid = register_connection(newsock, client_addr, 0, port);

			for (int i = 0; i < allfunc.size(); i++){
				(*allfunc[i])(clientid, client_addr);
				
			}
		}
		close(connections[id].sock);
	}
	template <class vfuncp>
	void start_listen_array(int port, vfuncp allfunc[], int funccount, int *return_id)
	{
		this->start_listen_vector(port, std::vector<vfuncp>(allfunc, allfunc+funccount) , return_id);
	}

	
	void close_connection(int id)
	{
		close(connections[id].sock);
	}
	void stop_listen(int id)
	{
		//stop the loop
		listening[id] = false;
		int port_listen = (connections[id].port);
		char ip[] = "127.0.0.1";
		printf("stop listening %s:%d\n", ip, port_listen);
		sockaddr_in addr = generate_addr(ip, port_listen, AF_INET);
		char tosend[] =  "\0";
		int sendid = this->send(addr, tosend, sizeof(tosend));
		close(connections[id].sock);
	}
};
std::map<int, struct connectionlist_node> TCP::connections = std::map<int, struct connectionlist_node>();
std::map<int, bool> TCP::listening = std::map<int, bool>();
int TCP::unused_id = 0;
#endif