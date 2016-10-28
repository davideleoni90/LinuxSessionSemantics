# LinuxSessionSemantics
<p align="justify">
This application is the result of a didactic project for the
<a href="http://www.dis.uniroma1.it/~quaglia/DIDATTICA/SO-II-6CRM/">
Operating Systems 2</a> course of the Master of Science of
Engineering in Computer Science at <a href="http://cclii.dis.uniroma1.it/?q=it/msecs">Sapienza University of Rome</a>.
The author of this project is <a href="https://www.linkedin.com/in/leonidavide">Davide Leoni</a>
</p>
<h2>Overview</h2>
<p align="justify">
This project is aimed at the introduction of a <i>session semantics</i> for files into the the Linux Kernel, by mean of a dedicated module.
</p>
<h2>Specifics</h2>
<p align="justify">
According to the session semantics, when a <i>file session</i> is opened by a process, all the modifications made to the original file are not visible by any other process until the session is closed by the process itself. As a consequence, the visible content of the file is always the result of the last closed file session.
<br>
In order to implement this new semantics, when a process calls the <i>open</i> system call requesting for a file session, the content of the file is entirely copied into a dynamically allocated memory buffer and all subsequent I/O operations possibly requested by the process on the file will affect only the buffer, not the original file itself. When the process closes the file, actually it closes the file session, so the content of the buffer is written into the original file, thus overriding all the previuous modifications possibly made by other processes.
<br>
In order to ensure that during a session a process can work on its "personal copy" of the the opened file without any interference by other processes, it's necessary to avoid using the <i>buffer cache</i>.
</p>
<h2>Implementation</h2>
<p align="justify">
No limit is imposed on the number of file sessions that can be opened at any time nor on the size of the file that can be opened using the session semantics, except for the obvious limit imposed by the available memory. It is also possible to add content to the file beyond its original filesize: in fact, as the buffer initially allocated to the session gets full, new pages are dynamically allocated to it in order to
satisfy the <i>write</i> request.
<br>
When a new session is opened, the module usage counter is incremented, and when the session is closed the counter is decremented. This guarantees that the module won't be removed from the system while there's an active file session.
<br>
As the module is installed into the Linux Kernel, the session semantics can be requested by using the usual system call <i>open</i> and OR-ing any flag with the flag <i>SESSION_OPEN</i> (given in the header <i>session.h</i>): in fact the underlying code replaces the open system call with a custom implementation that adds the chance of requesting the session semantics; the original version of the system call is obviously restored when the module is removed.
</p>
<h2>How to use</h2>
<p align="justify">
The module can be compiled and installed as any other module for the Linux Kernel.
The module was tested on Linux Kernel 2.6.34, with full preemption and SMP support, on x86 machine
<br>
The folder <i>UseCases</i> features some examples of usage of the module.
<br>
NOTE: every time a C user program ends, the <i>close</i> system call is implicitely invoked on the opened files of the current process by the
<i>exit</i> system call: as a consequence, if a file was opened adopting the session semantics, the content of the session will be flushed into the original file as the program finishes. 
</p>
