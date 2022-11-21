#!/usr/sbin/dtrace -s

pid$target::malloc:entry{ self->mem_size = arg0; }
pid$target::realloc:entry { self->old_ptr = arg0; self->mem_size = arg1; }
pid$target::calloc:entry { self->mem_size = arg0*arg1; }

pid$target::malloc:return, pid$target::realloc:return, pid$target::calloc:return  {
  printf("%u %u %p %p", timestamp, self->mem_size, arg1, self->old_ptr); 
  ustack();
  
  self->mem_size = 0;
  self->old_ptr = 0;
}

pid$target::free:entry /arg0!=0/ {printf("%u 0 %p",timestamp, arg0); ustack();}
