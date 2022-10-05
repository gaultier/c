function! s:copy_gitlab_url()
  echo "foO"
  let chan = sockconnect('tcp', '0.0.0.0:12345',{} )
  let file_path = expand('%:p')
  let line=line('.')
  call chansend(chan, [file_path, line])
  call chanclose(chan)
endfunction

nnoremap <leader>x :call <SID>copy_gitlab_url()<cr>
