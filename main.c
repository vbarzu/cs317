#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <cv.h>
#include <highgui.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>


#define MYPORT "9999" //the port users will be connecting to
#define BACKLOG 10    // how many pending connections queue will hold
#define STATE_INIT 0
#define STATE_READY 1
#define STATE_PLAY 2
#define STATE_PAUSE 3
#define SETUP 0
#define PLAY 1
#define PAUSE 2
#define TEARDOWN 4

timer_t play_timer;

struct RTSPClient {
    struct sockaddr_in client_addr;
    int fd;
    int RTPport;
    int state;// READY, PLAY
    int lastaction; // SETUP, PLAY, etc.
    int session;
    int seq;
	int scale;
} RTSPClient;


struct RTSPclientmsg{
    int cmd; // the defined value of SETUP, etc.
    int session;
    int seq;
    int port; // get the RTP port number from SETUP command
    int err; // 0: no err. else: err occured
	int scale;
    char Connection[ 1024 ];
    char Proxy_Require[ 1024 ];
    char Transport[ 1024 ];
    char Require[ 1024 ];
} RTSPclientmsg;

struct RTSPservermsg{
    int session;
    int seq;
    int err;
} RTSPservermsg;


void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void ClientInfo()
{
    RTSPClient.state = STATE_INIT;
    RTSPClient.session = 0;
}



void parseRTSPmessage(char* msg,int len,char* response)
{
	char myarray[1024];
	char* tempmsg = myarray;
	strcpy(tempmsg, msg);
    
	while(*tempmsg == "")
		tempmsg++;
    
	switch(*tempmsg) { /* go by the first character to start */
		case 'S': //for SETUP
			*(tempmsg+5)='\0';
			if(!strcmp(tempmsg+1, "ETUP")){
				RTSPclientmsg.cmd = SETUP;
				parseRTSPcmd(msg);
			}
			break;
		case 'P': //possibly PLAY or PAUSE
			*(tempmsg+4)='\0';
			if(!strcmp (tempmsg+1,"LAY")) {
				RTSPclientmsg.cmd = PLAY;
				parseRTSPcmd(msg);
			}
			else if(!strcmp (tempmsg+1,"AUS")){
				RTSPclientmsg.cmd = PAUSE;
				parseRTSPcmd(msg);
			}
			break;
		case 'T': //for TEARDOWN
			*(tempmsg+8)='\0';
			if(!strcmp(tempmsg+1,"EARDOWN")){
				RTSPclientmsg.cmd = TEARDOWN;
				parseRTSPcmd(msg);
			}
			break;
		default:
			break;
	}
}


void parseRTSPcmd(char* cmd) //parse whatever command was given with the headers
{
	char headerbuf[1024];
	char hdbuf[1024];
	char* headercontent = headerbuf;
	char* hd            = hdbuf;
    
    
	if(RTSPclientmsg.cmd == PLAY){
		hd = "Scale:";
		parse_request_headers(cmd,hd,headercontent);
		RTSPclientmsg.scale = atoi(headercontent);
        
	}
	
    
	hd = "client_port=";
	parse_request_headers(cmd,hd, headercontent);
	if(headercontent != 0)
		//because we want the client port as an int value its given to us as string
		RTSPclientmsg.port = atoi(headercontent);
    
	hd = "Session";
	parse_request_headers(cmd,hd,headercontent);
	if(headercontent != 0) RTSPclientmsg.session = atoi(headercontent);
	
	hd = "CSeq";
	parse_request_headers(cmd,hd, headercontent);
	RTSPclientmsg.seq = atoi(headercontent);
    
    
}


void parse_request_headers( char* msg, char* hd, char* hdcontent )
{
	char tmparray[1024];
	char rnarray[3];
	char headerarray[ strlen( hd )+1 ];
	char* tmp = tmparray;
	char* rn =  rnarray;
    char* header = headerarray;
	char cmp[] = "\r\n";
    
	if( !strcmp( hd, "client_port=" ) ){
		tmp = strstr( msg, hd );
		if( tmp == NULL ){
            hdcontent = 0;
            return;
        }
        else{
            tmp += strlen( hd );
            int port;
            sscanf( tmp, "%d", &port );
            // hdcontent = _itoa( port, hdcontent, 10 );
            return;
        }
    }
    
	strcpy( tmp, msg );
	int i;
	int len = strlen( msg );
	for( i = 0; i < len; i++ ){
        strncpy( rn, tmp, 2 );
        rn[2] = '\0';
        while( strcmp( rn, cmp ) && i < len ){
            tmp++; // find the end of a line
            i++;
            strncpy( rn, tmp, 2 );
            rn[2] = '\0';
        }
        tmp += 2;
        i += 2;
        while( *tmp == ' ' ){
            tmp++; // eliminate white space in the string
            i++;
        }
        
        strncpy( header, tmp, strlen(hd) );
        header[ strlen(hd) ] = '\0';
        if( !strcmp( header, hd ) ) break;
    }
	if( i >= len ){
        hdcontent = 0;
        return;
    }
    
	tmp += strlen( hd );
	while( *tmp == ' ' || *tmp == ':' ) tmp++;
    
	i = 0;
	while( tmp[i] != '\r' ) i++;
	tmp[ i ] = '\0';
	strcpy( hdcontent, tmp );
    
}


void serverResponse(int cmd, int code, char* response)
{
    if( code == 404 ){
        if( RTSPClient.session == 0 )
            sprintf( response, "RTSP/1.0 404 Not Found\r\nCSeq: %d\r\n\r\n", RTSPClient.seq );
        else
            sprintf( response, "RTSP/1.0 404 Not Found\r\nCSeq: %d\r\nSession: %d\r\n\r\n", RTSPClient.seq, RTSPClient.session );
        
    }
    else if( code == 200 ){
        switch( cmd ){
            case SETUP:
                sprintf( response, "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: %d\r\n\r\n", RTSPClient.seq, RTSPClient.session );
                break;
            case PLAY:
                sprintf( response, "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: %d\r\n\r\n", RTSPClient.seq, RTSPClient.session );
            case PAUSE:
                sprintf( response, "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: %d\r\n\r\n", RTSPClient.seq, RTSPClient.session );
            case TEARDOWN:
                sprintf( response, "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: %d\r\n\r\n", RTSPClient.seq, RTSPClient.session );
                break;
            default:
                if(RTSPClient.session == 0)
                    sprintf( response, "RTSP/1.0 501 Not Implemented\r\nCSeq: %d\r\n\r\n", RTSPClient.seq );
                else
                    sprintf( response, "RTSP/1.0 501 Not Implemented\r\nCSeq: %d\r\nSession:%d\r\n\r\n", RTSPClient.seq, RTSPClient.session );
                break;
        }
    }
    else{
        if(  RTSPClient.session == 0 )
            sprintf( response, "RTSP/1.0 501 Not Implemented\r\nCSeq: %d\r\n\r\n", RTSPClient.seq );
        else
            sprintf( response, "RTSP/1.0 501 Not Implemented\r\nCSeq: %d\r\nSession:%d\r\n\r\n", RTSPClient.seq, RTSPClient.session );
    }
}


CvCapture* client_requested_file()
{
    
	CvCapture *video;
	
    // Open the video file.
    video = cvCaptureFromFile("sample.avi");
    return video;
    
}


// This struct is created to save information that will be needed by the timer,
// such as socket file descriptors, frame numbers and video captures.
struct send_frame_data {
    CvCapture *vid;
    int socket;
    int scale;
    int frame_num;
};

// This function will be called when the timer ticks
void send_frame(union sigval sv_data) {
	IplImage *image;
  	CvMat *thumb;
	CvMat *encoded;
    
    
    struct send_frame_data *data = (struct send_frame_data *) sv_data.sival_ptr;
    
    
    
    // You may retrieve information from the caller using data->field_name
    // ...
    
    printf("\nthis is frame_num=%d\n", data->frame_num);
    printf("\nthis is data scale=%d\n", data->scale);
    // Obtain the next frame from the video file
    while(1){
        
        image = cvQueryFrame(data->vid);
        if (image == NULL) {
            // Close the video file
            cvReleaseCapture(&data->vid);
            stop_timer();
            break;
            
        }
        
        
        int WIDTH = 300;
        int HEIGHT = 300;
        
        // Convert the frame to a smaller size (WIDTH x HEIGHT)
        thumb = cvCreateMat(WIDTH, HEIGHT, CV_8UC3);
        cvResize(image, thumb, CV_INTER_AREA);
        
        
        
        // Encode the frame in JPEG format with JPEG quality 30%.
        const static int encodeParams[] = { CV_IMWRITE_JPEG_QUALITY, 30 };
        encoded = cvEncodeImage(".jpeg", thumb, encodeParams);
        // After the call above, the encoded data is in encoded->data.ptr
        // and has a length of encoded->cols bytes.
        
        int jpeg_size = encoded->cols;
        int rtp_header_size = 12;
        char rtp_buffer[jpeg_size + rtp_header_size + 4];
        int rtp_pk_size = rtp_header_size + jpeg_size;
        int ts = data->scale * 40;
        
        
        
        //First 4 bytes of the packet before the RTSP header
		rtp_buffer[0] = '$';
		rtp_buffer[1] = 0;
		rtp_buffer[2] = (rtp_pk_size & 0x0000FF00) >> 8;
		rtp_buffer[3] = (rtp_pk_size & 0x000000FF);
        //Now the rest of this is the RTP Header
		rtp_buffer[4] = 0x80;           //RTP Version
		rtp_buffer[5] = 0x9a;           //Payload type = 26
		rtp_buffer[7]  = data->frame_num & 0x0FF;           // each packet is counted with a sequence counter
		rtp_buffer[6]  = data->frame_num >> 8;
		rtp_buffer[11] = (ts & 0x000000FF);
		rtp_buffer[10] = (ts & 0x0000FF00) >> 8;
		rtp_buffer[9]  = (ts & 0x00FF0000) >> 16;
		rtp_buffer[8]  = (ts & 0xFF000000) >> 24;
		rtp_buffer[12] = 0x00;                               // 4 byte SSRC (sychronization source identifier)
		rtp_buffer[13] = 0x00;                               // we just an arbitrary number here to keep it simple
		rtp_buffer[14] = 0x00;
		rtp_buffer[15] = 0x00;
        
        
		//this is to append the JPEG data to the rtp buffer
		memcpy(&rtp_buffer[16],encoded->data.ptr, encoded->cols);
        
		data->frame_num++;
		//Send everything in the buffer, everything being the buffer + 4 byte added header and the jpeg data
		printf("'\nman this is the frame number=%d\n",data->frame_num);
		int scaletracker = data->frame_num % data->scale;
        if(scaletracker == 0){
			int x =	send(data->socket, rtp_buffer,rtp_pk_size + 4, 0);
			data->frame_num++;
			printf("\n%d\n",x);   //Just to make sure were not getting -1, in which case I'll know that data is not being sent over the socket
			return;
        }
        
    }
    
    
}

void stop_timer(void) {
    
    
    struct itimerspec play_interval;
    
    // The following snippet is used to stop a currently running timer. The current
    // task is not interrupted, only future tasks are stopped.
    play_interval.it_interval.tv_sec = 0;
    play_interval.it_interval.tv_nsec = 0;
    play_interval.it_value.tv_sec = 0;
    play_interval.it_value.tv_nsec = 0;
    timer_settime(play_timer, 0, &play_interval, NULL);
    
}


int main(int argc, char* argv[])
{
	char* port;
    
	if(argc=2)
    {
        port = (argv[1]);
    }
    
	int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    struct sigaction sa;
	int yes =1;
    char s[INET6_ADDRSTRLEN];
    int rv;
	socklen_t sin_size;
	ssize_t result;
	const char *buffer[1024] = { 0 };
	char clientaddrport[1024];
	int connected = 1;
	srand(time(NULL));
    
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    
    if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }
    
    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }
        
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }
        
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        
        break;
    }
    
    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        return 2;
    }
    
    freeaddrinfo(servinfo); // all done with this structure
    
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }
    
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    
    printf("server: waiting for connections...\n");
	RTSPClient.state = STATE_INIT;
    
    while(1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }
        
        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *)&their_addr),
                  s, sizeof s);
        printf("server: got connection from %s\n", s);
		int pid = fork();
		if(pid==0){
            while(yes){
                
                char buf[1024];
                char *buffer = buf;
                int messRec = recv(new_fd, buffer, 1024, 0);
                if(messRec == -1)
                {
                    printf("Error receiving message\n");
                    exit(-1);
                }
                if(messRec == 0)
                {
                    printf("Client disconnected\n");
                    exit(0);
                }
                printf("\nRTSP Client request:\n");
                printf("\n%s\n", buffer);
                
                
                char response[1024];
                char* resp = response;
                int len = strlen(buffer);
                
                parseRTSPmessage(buffer,len, response);
                
                struct send_frame_data data;
                
                if(RTSPclientmsg.cmd == SETUP){
                    if(RTSPClient.state == STATE_READY || RTSPClient.state == STATE_PLAY || RTSPClient.state == STATE_PAUSE){
                        serverResponse(SETUP,404,response);
                    }
                    else{
                        RTSPClient.state = STATE_READY;
                        RTSPClient.lastaction = 1;
                        RTSPClient.seq   = RTSPclientmsg.seq;
                        RTSPClient.session = rand();
                        CvCapture *x = client_requested_file();
                        printf("\n%d\n",RTSPClient.state);
                        if(x == NULL){
                            serverResponse(SETUP,404,response);
                        }
                        else{
                            serverResponse(SETUP,200,response);
                        }
                        cvReleaseCapture(&x);
                    }
                }
                
                else if(RTSPclientmsg.cmd == PLAY) {
                    if(RTSPClient.state == STATE_INIT){
                        serverResponse(PLAY,404,response);
                    }
                    else{
                        if(RTSPClient.state == STATE_READY){
                            CvCapture *g = client_requested_file();
                            if(g == NULL) {//try to see if the file their trying to open exists
                                RTSPClient.seq = RTSPclientmsg.seq;
                                serverResponse(PLAY, 404, response);
                            }
                            else{
                                RTSPClient.state = STATE_PLAY;
                                RTSPClient.lastaction = RTSPclientmsg.cmd;
                                RTSPClient.seq = RTSPclientmsg.seq;
                                // RTSPClient.scale = RTSPclientmsg.scale;
                                serverResponse(PLAY, 200, response );
                                
                                
                                data.vid = g;
                                data.socket = new_fd;
                                data.scale = RTSPclientmsg.scale;
                                struct sigevent play_event;
                                struct sigevent play_data;
                                struct itimerspec play_interval;
                                
                                
                                memset(&play_event, 0, sizeof(play_event));
                                play_event.sigev_notify = SIGEV_THREAD;
                                play_event.sigev_value.sival_ptr = &data;
                                play_event.sigev_notify_function = send_frame;
                                
                                
                                play_interval.it_interval.tv_sec = 0;
                                play_interval.it_interval.tv_nsec = 40 * 1000000; // 40 ms in ns
                                play_interval.it_value.tv_sec = 0;
                                play_interval.it_value.tv_nsec = 1; // can't be zero
                                
                                timer_create(CLOCK_REALTIME, &play_event, &play_timer);
                                timer_settime(play_timer, 0, &play_interval, NULL);
                            }
                            
                        }
                        else{
                            RTSPClient.state = STATE_PLAY;
                            RTSPClient.lastaction = RTSPclientmsg.cmd;
                            RTSPClient.seq = RTSPclientmsg.seq;
                            serverResponse(PLAY, 200, response );
                            
                            data.scale = RTSPclientmsg.scale;
                            struct sigevent play_event;
                            struct sigevent play_data;
                            struct itimerspec play_interval;
                            
                            memset(&play_event, 0, sizeof(play_event));
                            play_event.sigev_notify = SIGEV_THREAD;
                            play_event.sigev_value.sival_ptr = &data;
                            play_event.sigev_notify_function = send_frame;
                            
                            
                            play_interval.it_interval.tv_sec = 0;
                            play_interval.it_interval.tv_nsec = 40 * 1000000; // 40 ms in ns
                            play_interval.it_value.tv_sec = 0;
                            play_interval.it_value.tv_nsec = 1; // can't be zero
                            
                            //We need not recreate a timer in this situation, we just want to resume
                            //the one that was stopped when the video was paused.
                            timer_settime(play_timer, 0, &play_interval, NULL);
                            
                        }
                    }
                }
                
                else if(RTSPClient.state == STATE_PLAY && RTSPclientmsg.cmd == PAUSE){
                    RTSPClient.state = STATE_PAUSE;
                    RTSPClient.lastaction = RTSPclientmsg.cmd;
                    RTSPClient.seq        = RTSPclientmsg.seq;
                    serverResponse(PAUSE, 200, response);
                    stop_timer();
                }
                
                else if( RTSPclientmsg.cmd == TEARDOWN ){
                    if(RTSPClient.state == STATE_INIT){
                        serverResponse(TEARDOWN,404,response);
                    }
                    else{
                        if(RTSPClient.state == STATE_PAUSE){
                            RTSPClient.state = STATE_INIT; 
                            RTSPClient.lastaction = RTSPclientmsg.cmd; 
                            RTSPClient.seq = RTSPclientmsg.seq; 
                            serverResponse(TEARDOWN, 200, response );
                            // The following line is used to delete a timer.
                            stop_timer();
                            timer_delete(play_timer);
                            cvReleaseCapture(&data.vid);
                            connected = 0;  //dont know where this should be right now remember to deal with this later
                        }
                        else{
                            if(RTSPClient.state == STATE_READY){
                                RTSPClient.state = STATE_INIT; 
                                RTSPClient.lastaction = RTSPclientmsg.cmd; 
                                RTSPClient.seq = RTSPclientmsg.seq; 
                                serverResponse(TEARDOWN, 200, response );
                            }
                            else{
                                if(RTSPClient.state == STATE_PLAY) {
                                    RTSPClient.state = STATE_INIT; 
                                    RTSPClient.lastaction = RTSPclientmsg.cmd; 
                                    RTSPClient.seq = RTSPclientmsg.seq; 
                                    serverResponse(TEARDOWN, 200, response );
                                    stop_timer();
                                    timer_delete(play_timer);
                                    //	cvReleaseCapture(&data.vid);
                                }
                                
                            }
                        }
                    }
                }
                
                else if(RTSPclientmsg.cmd == PAUSE){
                    if(RTSPClient.state == STATE_INIT || RTSPClient.state == STATE_READY){
                        serverResponse(PAUSE,404,response);
                    }
                }
                
                else{
                    serverResponse(0, 501, response);
                    printf("\n%s\n", response);
                }
                
                
                printf("\nResponse sent to client:\n\n%s", response);
                send(new_fd,resp,strlen(resp),0);
                memset(&buffer[0], 0, sizeof(buffer));
                
                /*
                 if(connected == 0){
                 closesocket(new_fd); //the TEARDOWN happens here
                 printf("\nClient disconnected\n");
                 }
                 */
                
            } 
        }
        else{
            if (pid == -1){
                perror("fork");
                return 1;
            }
            else{
                close(new_fd);
            }
        }
        printf("\nclosed this socket \n");
        close (new_fd);
        
		
    } 
   	
    return 0;
}