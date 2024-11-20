#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <bits/stdc++.h>
#include <pthread.h>
#include <dirent.h>


#define SILLY_LISTENER_PORT 7999        
#define SILLY_CLIENT_PORT 8000
#define SILLY_TCP_PORT 8001
#define SILLY_LISTENER_TCP_PORT 8002


#define BUF_SIZE 4096 

using namespace std;


const char* localIP="192.168.156.31";
char* myId;

map<string,string> clientMap;
char* leader="192.168.156.31";
bool isConnected=false;
bool isLeader=false;

bool starts_with(const char *sentence, const char *prefix) {
    size_t len_prefix = strlen(prefix);
    size_t len_sentence = strlen(sentence);

    if (len_sentence < len_prefix) {
        return false;
    }

    return strncmp(sentence, prefix, len_prefix) == 0;
}

string generateHexadecimal(int length) {
    const char hex_chars[] = "0123456789ABCDEF";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::stringstream ss;
    for (int i = 0; i < length; ++i) ss << hex_chars[dis(gen)];
    return ss.str();
}

int onLeaderElected(){
    sockaddr_in destAddr;
    const char* request="SEND FILE LOG";
    map<string,vector<string>> fileMap;
    for (auto it:clientMap){
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        memset(&destAddr,0,sizeof(destAddr));
        destAddr.sin_family=AF_INET;
        destAddr.sin_port=htons(SILLY_LISTENER_TCP_PORT);
        const char* f=it.first.c_str();
        destAddr.sin_addr.s_addr=inet_addr(f);
        string ip=it.first.c_str();
        if (inet_pton(AF_INET, ip.c_str(), &destAddr.sin_addr) <= 0) {
            cerr << "Invalid IP address: " << ip << std::endl;
            close(sock);
            return -1;
        }
        if (connect(sock, (struct sockaddr*)&destAddr, sizeof(destAddr)) < 0) {
            close(sock);
            return -1;
        }
        if (send(sock, request, strlen(request), 0) < 0) {
            perror("Send request failed");
            close(sock);
            return -1;
        }
        char buffer[BUF_SIZE];
        int bytesReceived = recv(sock, buffer, BUF_SIZE - 1, 0);
        if (bytesReceived < 0) {
            perror("Receive failed");
        }else{
            buffer[bytesReceived] = '\0';
            string response(buffer);
            size_t pos = 0;
            string delimiter = "\n";
            while ((pos = response.find(delimiter)) != std::string::npos) {
                std::string fileName = response.substr(0, pos);
                response.erase(0, pos + delimiter.length());
                fileMap[fileName].push_back(ip);
            }
        }
        close(sock);
    }
    std::ofstream outFile("filemap.txt");
    if (!outFile.is_open()) {
        std::cerr << "Error opening filemap.txt for writing." << std::endl;
        return -1;
    }
    for (const auto& entry : fileMap) {
        outFile << entry.first; 
        for (const auto& ip : entry.second) {
            outFile << " " << ip; 
        }
        outFile << "\n";
    }
    outFile.close();
    std::cout << "File map written to filemap.txt successfully." << std::endl;
    return 0;
}

void DieWithError(const char *errorMessage) {
    perror(errorMessage);
    exit(EXIT_FAILURE);
}

void listen_for_broadcast(int sock,int sock2){
    char buffer[BUF_SIZE];
    struct sockaddr_in clientAddr;
    socklen_t addrSize;
    int timer=0;
    while (1) {
        addrSize = sizeof(clientAddr);
        cout << "Listening for broadcasts..." << endl;  
        int bytesReceived = recvfrom(sock, buffer, BUF_SIZE, 0, (struct sockaddr *)&clientAddr, &addrSize);
        if (bytesReceived < 0) DieWithError("recvfrom() failed");
        else if (bytesReceived>0 && strcmp(inet_ntoa(clientAddr.sin_addr),localIP)!=0){
                const char* senderIP=inet_ntoa(clientAddr.sin_addr);
                if (strcmp(buffer,"Hey! I am alone, Care to join me?")==0){
                    cout<<"Received Broadcast"<<endl;
                    const char* ack_msg="Yes! I wanna join you";
                    clientAddr.sin_port=htons(SILLY_LISTENER_PORT);
                    if (sendto(sock2,ack_msg,strlen(ack_msg),0,(struct sockaddr*)&clientAddr,sizeof(clientAddr))<0) DieWithError("Error in sending ack");
                    cout<<"Sent Ack"<<endl;
                }
                else if (strcmp(buffer,"Yes! I wanna join you")==0 && strcmp(senderIP,localIP)!=0){
                    cout<<"Received Ack"<<endl;
                    string newID=generateHexadecimal(10);
                    if (clientMap.find(senderIP)==clientMap.end()){
                        char* id_msg="Your ID is: ";
                        strcat(id_msg,newID.c_str());
                        if (sendto(sock2,id_msg,strlen(id_msg),0,(struct sockaddr*)&clientAddr,sizeof(clientAddr))<0) DieWithError("Error in sending ID");
                        cout<<"New ID sent"<<endl;
                    }       
                }
                else if (starts_with(buffer,"Your ID is: ") && !isConnected){
                    char* trail="Your ID is: ";
                    char* newID=buffer+strlen(trail);
                    isConnected=true;
                    myId=newID;
                    char* declare="My ID is: ";
                    strcat(declare,newID);
                    sockaddr_in broadcastAddr;
                    memset(&broadcastAddr,0,sizeof(broadcastAddr));
                    broadcastAddr.sin_family=AF_INET;
                    broadcastAddr.sin_port=htons(SILLY_LISTENER_PORT);
                    broadcastAddr.sin_addr.s_addr=inet_addr("192.168.156.255");
                    if (sendto(sock2,declare,strlen(declare),0,(struct sockaddr*)&broadcastAddr,sizeof(broadcastAddr))<0) DieWithError("Error in sending ID");
                }
                else if (starts_with(buffer,"My ID is: ")){
                    char* trail="My ID is: ";
                    char* newID=buffer+strlen(trail);
                    clientMap[senderIP]=newID;
                    int rc=fork();
                    if (rc<0) DieWithError("Error in forking");
                    else if (rc==0){
                        FILE* fptr=fopen("clientMap.txt","a");
                        fprintf(fptr,"%s %s\n",senderIP,newID);
                        fclose(fptr);
                        exit(EXIT_SUCCESS);
                    }
                    if (isLeader){
                        string x="Mappings:\n";
                        for (auto it:clientMap){
                            x+=it.first+" "+it.second+"\n";
                        }
                        const char* fans=x.c_str();
                        if (sendto(sock2,fans,strlen(fans),0,(struct sockaddr*)&clientAddr,sizeof(clientAddr))<0) DieWithError("Cant send");
                        vector<pair<string,string>> clientList;
                        for (auto it:clientMap){
                            clientList.push_back({it.second,it.first});
                        }
                        std::sort(clientList.begin(),clientList.end());
                        string newLeader=clientList[0].second;
                        const char* nLeader=newLeader.c_str();
                        if (strcmp(leader,nLeader)!=0){
                            isLeader=false;
                            leader = new char[strlen(nLeader) + 1];
                            strcpy(leader, nLeader);
                            sockaddr_in broadcastAddr;
                            memset(&broadcastAddr,0,sizeof(broadcastAddr));
                            broadcastAddr.sin_family=AF_INET;
                            broadcastAddr.sin_port=htons(SILLY_LISTENER_PORT);
                            broadcastAddr.sin_addr.s_addr=inet_addr("192.168.156.255");
                            char* newLeadMsg="New Leader: ";
                            strcat(newLeadMsg,leader);
                            if (sendto(sock2,newLeadMsg,strlen(newLeadMsg),0,(struct sockaddr*)&broadcastAddr,sizeof(broadcastAddr))<0) DieWithError("Cant send");
                        }
                        else{
                            char* leaderMsg="Leader At: ";
                            strcat(leaderMsg,localIP);
                            if (sendto(sock2,leaderMsg,strlen(leaderMsg),0,(struct sockaddr*)&clientAddr,sizeof(clientAddr))<0) DieWithError("Cant do");
                        }

                    }
                }
                else if (starts_with(buffer,"Mapping:\n")){
                    char* trail="Mapping:\n";
                    char* pos=buffer+strlen(trail);
                    FILE* fptr=fopen("clientMap.txt","w");
                    fprintf(fptr,"%s",pos);
                    fclose(fptr);
                    ifstream inputFile("client_map.txt");
                    string key, value;
                    while (inputFile >> key >> value) clientMap[key] = value; 
                    inputFile.close();
                }
                else if (starts_with(buffer,"New Leader: ")){
                    if (strcmp(senderIP,leader)){
                        char* trail="New Leader: ";
                        char* newLeader=buffer+sizeof(trail);
                        leader=newLeader;
                        if (strcmp(leader,localIP)){
                            isLeader=true;
                            int x = onLeaderElected();
                            if (x!=0) DieWithError("Error in itialising");
                        }
                    }
                }
                else if (starts_with(buffer,"Leader At: ")){
                    char* trail="Leader At: ";
                    leader=buffer+strlen(trail);
                }
        }
        buffer[bytesReceived] = '\0';
        printf("Received message from %s:%d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
        printf("Message: %s\n", buffer);
    }
}

void send_broadcast(int sock,int sock2){
    const char* broadcast_msg="Hey! I am alone, Care to join me?";
    const char* broadcastIP="192.168.156.255";
    sockaddr_in broadcastAddr;
    memset(&broadcastAddr,0,sizeof(broadcastAddr));
    broadcastAddr.sin_family=AF_INET;
    broadcastAddr.sin_port=htons(SILLY_LISTENER_PORT);
    broadcastAddr.sin_addr.s_addr=inet_addr(broadcastIP);
    int counter=0;
    while (!isConnected && counter<3){
        if (sendto(sock,broadcast_msg,strlen(broadcast_msg),0,(struct sockaddr*)&broadcastAddr,sizeof(broadcastAddr))<0){
           DieWithError("Error in sending broadcast");
        }
        cout<<"Sent Broadcast for connecting to request"<<endl;
        sleep(2);
        counter++;
    }
    if (counter==3){
        /*THIS MEANS THERE IS NO OTHER ONE IN THE NETWORK*/
        isLeader=true;
        char temp_buffer[BUF_SIZE];
        strcpy(temp_buffer,localIP);
        leader=temp_buffer;
        string my_id=generateHexadecimal(40);
        myId=new char[my_id.size()+1];
        strcpy(myId,my_id.c_str());
        isConnected=true;
        FILE* fptr=fopen("clientMap.txt","w");
        fprintf(fptr,"%s %s\n",localIP,myId);
        fclose(fptr);
    }
}

vector<string> listFilesInDirectory() {
    std::vector<std::string> files;
    DIR* dir = opendir(".");
    if (dir == nullptr) {
        perror("Unable to open directory");
        return files;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            files.push_back(entry->d_name);
        }
    }
    closedir(dir);
    return files;
}


void handleClient(int clientSock, sockaddr_in clientAddr) {
    char buffer[BUF_SIZE];
    int bytesReceived = recv(clientSock, buffer, BUF_SIZE - 1, 0);
    if (bytesReceived < 0) {
        perror("Receive failed");
        close(clientSock);
        return;
    }
    buffer[bytesReceived] = '\0';
    std::string senderIP = inet_ntoa(clientAddr.sin_addr);
    std::cout << "Request from " << senderIP << ": " << buffer << std::endl;
    if (senderIP == leader) {
        std::vector<std::string> files = listFilesInDirectory();
        std::string response = "Files in directory:\n";
        for (const auto& file : files) {
            response += file + "\n";
        }
        send(clientSock, response.c_str(), response.size(), 0);
        std::cout << "Response sent to leader: " << senderIP << std::endl;
    } else {
        std::cout << "Request ignored: not from leader" << std::endl;
    }

    close(clientSock);
}

void listen_for_file_log(){
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        perror("Socket creation failed");
        return;
    }

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SILLY_LISTENER_TCP_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    if (bind(serverSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Bind failed");
        close(serverSock);
        return;
    }
    if (listen(serverSock, 5) < 0) {
        perror("Listen failed");
        close(serverSock);
        return;
    }
    while (true) {
        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int clientSock = accept(serverSock, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientSock < 0) {
            perror("Accept failed");
            continue;
        }
        handleClient(clientSock, clientAddr);
    }
    close(serverSock);
}

int main() {
    int sock;
    struct sockaddr_in serverAddr;
    if ((sock = socket(AF_INET, SOCK_DGRAM,IPPROTO_UDP)) < 0) {
        DieWithError("Socket creation failed");
    }
    int broadcastPermission = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastPermission, sizeof(broadcastPermission)) < 0) {
        DieWithError("Setting socket options failed");
    }
    memset(&serverAddr, 0, sizeof(serverAddr));          
    serverAddr.sin_family = AF_INET;                     
    serverAddr.sin_addr.s_addr = INADDR_ANY;              
    serverAddr.sin_port = htons(SILLY_LISTENER_PORT);                   
    if (bind(sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        DieWithError("Binding failed for listener");
    }
    sockaddr_in client_socket;
    int sock2=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    memset(&client_socket,0,sizeof(client_socket));
    client_socket.sin_family=AF_INET;
    client_socket.sin_port=htons(SILLY_CLIENT_PORT);
    client_socket.sin_addr.s_addr=INADDR_ANY;
    if (bind(sock2,(struct sockaddr*)&client_socket,sizeof(client_socket))<0){
        DieWithError("Binding failed for client");
    }
    if (setsockopt(sock2, SOL_SOCKET, SO_BROADCAST, &broadcastPermission, sizeof(broadcastPermission)) < 0) {
        DieWithError("Setting socket options failed");
    }
    cout<<"All sockets are set"<<endl;  
    thread t1(listen_for_broadcast,sock,sock2);
    thread t2(send_broadcast,sock,sock2);
    thread t3(listen_for_file_log);
    t1.join();
    t2.join();
    t3.join();
}