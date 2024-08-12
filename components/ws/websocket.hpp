#ifndef BASECLASS_H
#define BASECLASS_H

class WebSocketClient
{
public:
  virtual int connect();
  virtual int read();
  virtual int write();
  virtual ~WebSocketClient();
};


#endif // BASECLASS_H