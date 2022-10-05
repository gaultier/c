function! s:copy_gitlab_url()
  let file_path = expand('%:p')
  let line=line('.')
   call jobstart(['/Users/pgaultier/code/c/nvim/gitlab-url-copy', file_path, line], {})
endfunction

nmap <leader>x :call <SID>copy_gitlab_url()<cr>
