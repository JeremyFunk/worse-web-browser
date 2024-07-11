#include "http.h"

int main(int argc,char *argv[])
{
    HTTP http("www.google.com", 80);
    HTTPResponse response = http.get("/");
    printResponse(response);



    return 0;
}