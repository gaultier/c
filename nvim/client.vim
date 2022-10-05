let chan = sockconnect('tcp', '0.0.0.0:12345', {'rpc': v:true})
echo 'requesting'
let result = rpcrequest(chan, 'nvim_get_api_info')
