let chan = sockconnect('tcp', '0.0.0.0:12345',{} )
echo 'requesting'
" let result = rpcrequest(chan, 'hello/world')
call chansend(chan, "hello world")
call chanclose(chan)
echo 'requested'
