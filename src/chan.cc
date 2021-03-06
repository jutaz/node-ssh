#include "chan.h"

Persistent<FunctionTemplate> Chan::constructor_template;

Chan::Chan() {
    done = false;
    channel = NULL;
}

void Chan::Initialize() {
    HandleScope scope;
    
    Local<FunctionTemplate> t = FunctionTemplate::New(New);
    constructor_template = Persistent<FunctionTemplate>::New(t);
    constructor_template->Inherit(EventEmitter::constructor_template);
    constructor_template->InstanceTemplate()->SetInternalFieldCount(1);
    constructor_template->SetClassName(String::NewSymbol("Channel"));
    
    NODE_SET_PROTOTYPE_METHOD(constructor_template, "write", Write);
}

Handle<Value> Chan::New(const Arguments &args) {
    HandleScope scope;
    
    Chan *chan = new Chan();
    chan->Wrap(args.This());
    
    return args.This();
}

Handle<Value> Chan::Create(ssh_channel chan) {
    HandleScope scope;
    
    Handle<Object> obj = Chan::constructor_template
        ->GetFunction()->NewInstance();
    Chan *c = ObjectWrap::Unwrap<Chan>(obj);
    c->channel = chan;
    c->Ref();
    
    eio_custom(ReadChannel, EIO_PRI_DEFAULT, ReadChannelAfter, c);
    return obj;
}

Handle<Value> Chan::Write(const Arguments &args) {
    HandleScope scope;
    
    Chan *c = ObjectWrap::Unwrap<Chan>(args.This());
    Handle<Object> bufObj = Handle<Object>::Cast(args[0]);
    
    char *data = Buffer::Data(bufObj);
    size_t len = Buffer::Length(bufObj);
    
    ssh_channel_write(c->channel, data, len);
    
    return Undefined();
}

int Chan::ReadChannel(eio_req *req) {
    Chan *chan = (Chan *) req->data;
    char *buf = new char[2048];
    int i = 0;
    do {
        i = ssh_channel_read_nonblocking(chan->channel, buf, 2048, 0);
    } while (i == 0);
    if (i < 0) chan->done = true;
    
    std::pair<int, char *> pair(i, buf);
    chan->buffers.push_back(pair);
    
    return 0;
}

int Chan::ReadChannelAfter(eio_req *req) {
    HandleScope scope;
    
    Chan *chan = (Chan *) req->data;
    
    while (!chan->buffers.empty()) {
        std::pair<int, char *> p = chan->buffers.front();
        chan->buffers.pop_front();
        if (p.first == 0) {
            chan->Emit(String::NewSymbol("end"), 0, NULL);
        }
        else {
            Buffer *b = Buffer::New(p.second, p.first);
            b->Ref();
            
            Handle<Value> argv[1];
            argv[0] = b->handle_;
            chan->Emit(String::NewSymbol("data"), 1, argv);
            delete p.second;
        }
    }
    
    if (!chan->done) {
        eio_custom(ReadChannel, EIO_PRI_DEFAULT, ReadChannelAfter, chan);
    }
    
    return 0;
}
