#!/usr/sbin/dtrace -s

pid$target::malloc:entry{printf("%u %u",timestamp, arg0); ustack();}
pid$target::realloc:entry {printf("%u %u %u", timestamp, arg0, arg1); ustack();}
pid$target::calloc:entry {printf("%u %u %u", timestamp, arg0, arg1); ustack();}

pid$target::malloc:return, pid$target::realloc:return, pid$target::calloc:return  {printf("%u %p", timestamp, arg1); ustack();}

pid$target::free:entry /arg0!=0/ {printf("%u %p",timestamp, arg0); ustack();}
