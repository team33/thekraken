

Contents:
0. License
1. What does The Kraken do?
2. How is The Kraken useful?
3. Supported configurations (PLEASE READ!)
4. Building The Kraken
4.1. Pre-requisites
4.2. Building
5. Installation
5.1. V6 client
5.2. V7 client
6. How do I know it's working?
7. Known issues and caveats
8. Support



0. License

  The Kraken is licensed under GNU General Public License version 2.
  See COPYING.txt for details.



1. What does The Kraken do?

  The Kraken wraps around FahCore binaries and sets CPU affinity as soon
  as subsequent worker threads get created.



2. How is The Kraken useful?

  Linux scheduler(s) tend to make sub-optimal decisions as far as FahCores
  are concerned. Multinode NUMA machines are affected most; local worker
  thread's memory may get allocated (paged in) on one node but then same
  worker thread usually gets migrated to a processor on another node thus
  defeating the concept of local memory.

  The Kraken sets CPU affinity at earliest possible moment thus ensuring
  that worker threads always use local memory (same-node page-ins).



3. Supported configurations

  The Kraken is supported on modern x86-64 Linux distributions running
  Folding@Home clients 6.34 and 7.

  The Kraken supports FahCores A3 and A5. Support for other FahCores
  may be added in future, as required.

  The Kraken supports single SMP client. Multi-SMP-client configurations
  are not supported at this time.


4. Building The Kraken

4.1. Pre-requisites

    1. Make sure that (where appropriate)
       - NUMA is enabled,
       - node interleave is disabled,
       - ACPI SRAT is enabled

    2. Make sure that make and gcc are installed; on Ubuntu issue

       sudo apt-get install gcc make

    3. Make sure that all target FahCores have been downloaded
       by the client.



4.2. Building

    1. Run "make"
    2. Run "sudo make install" (or call "make install" as root)
    
    Voila. The Kraken is available in /usr/bin.



5. Installation

5.1. V6 client

    Stop the client. Then go to client directory and run:

      thekraken -i

    Doing so should result in something along the following:

    $ thekraken -i
    thekraken: The Kraken 0.3 (compiled Sat Apr  2 11:38:47 MDT 2011 by fah@tentacle)
    thekraken: Processor affinity wrapper for Folding@Home
    thekraken: The Kraken comes with ABSOLUTELY NO WARRANTY; licensed under GPLv2
    thekraken: performing installation to .
    thekraken: /home/fah/fah-6.34/FahCore_a3.exe: wrapper succesfully installed
    thekraken: /home/fah/fah-6.34/FahCore_a5.exe: wrapper succesfully installed
    thekraken: finished installation, 2 out of 2 files processed
    $ 

    Re-start the client.



5.2. V7 client

    Stop the client. Run:

      sudo thekraken -i /var/lib/fahclient/cores

    Alternatively, run "thekraken -i /var/lib/fahclient/cores" as root.

    Doing so should result in something along the following:

    $ sudo thekraken -i /var/lib/fahclient/cores/
    thekraken: The Kraken 0.3 (compiled Sat Apr  2 11:38:47 MDT 2011 by fah@tentacle)
    thekraken: Processor affinity wrapper for Folding@Home
    thekraken: The Kraken comes with ABSOLUTELY NO WARRANTY; licensed under GPLv2
    thekraken: performing installation to /var/lib/fahclient/cores/
    thekraken: descend into /var/lib/fahclient/cores/www.stanford.edu and all other subdirectories [Y/n]?

    If path appears correct, confirm with "y" and press Enter. This should result
    in something along the following:
    
    thekraken: /var/lib/fahclient/cores/www.stanford.edu/~pande/Linux/AMD64/Core_a3.fah/FahCore_a3: wrapper succesfully installed
    thekraken: /var/lib/fahclient/cores/www.stanford.edu/~pande/Linux/AMD64/beta/Core_a3.fah/FahCore_a3: wrapper succesfully installed
    thekraken: finished installation, 2 out of 2 files processed
    $ 

    Re-start the client.



6. How do I know it's working?

    While folding, run:
   
      for i in $(ps -L auxw | grep FahCo  | awk '{ if ($4 > 10) print $3 }' | sort -n -k1,1) ; do taskset -pc $i  ; done

    Doing so should give you sequential processor numbers starting with 0 in its
    output, i.e.:

    pid 41666's current affinity list: 0
    pid 41669's current affinity list: 1
    pid 41670's current affinity list: 2
    pid 41671's current affinity list: 3
    pid 41672's current affinity list: 4
    pid 41673's current affinity list: 5
    pid 41674's current affinity list: 6
    pid 41675's current affinity list: 7
    pid 41676's current affinity list: 8
    pid 41677's current affinity list: 9
    pid 41678's current affinity list: 10
    pid 41679's current affinity list: 11
    pid 41680's current affinity list: 12
    pid 41681's current affinity list: 13
    pid 41682's current affinity list: 14
    pid 41683's current affinity list: 15
    pid 41684's current affinity list: 16
    pid 41685's current affinity list: 17
    pid 41686's current affinity list: 18
    pid 41687's current affinity list: 19
    pid 41688's current affinity list: 20
    pid 41689's current affinity list: 21
    pid 41690's current affinity list: 22
    pid 41691's current affinity list: 23
    pid 41692's current affinity list: 24
    pid 41693's current affinity list: 25
    pid 41694's current affinity list: 26
    pid 41695's current affinity list: 27
    pid 41696's current affinity list: 28
    pid 41697's current affinity list: 29
    pid 41698's current affinity list: 30
    pid 41699's current affinity list: 31
    pid 41700's current affinity list: 32
    pid 41701's current affinity list: 33
    pid 41702's current affinity list: 34
    pid 41703's current affinity list: 35
    pid 41704's current affinity list: 36
    pid 41705's current affinity list: 37
    pid 41706's current affinity list: 38
    pid 41707's current affinity list: 39
    pid 41708's current affinity list: 40
    pid 41709's current affinity list: 41
    pid 41710's current affinity list: 42
    pid 41711's current affinity list: 43
    pid 41712's current affinity list: 44
    pid 41713's current affinity list: 45
    pid 41714's current affinity list: 46
    pid 41715's current affinity list: 47

   
   
7. Know issues and caveats

    CAUTION: FahCore_a5 is known to be problematic at user-induced shutdowns*.
             To be on a safe side make a backup of complete client directory
             before hitting Ctrl+C!

    To tell whether checkpoint was written correctly check the size
    of work/wudata_XX.ckp file (XX being current slot number).
    It should be 75160 (for core A5). If it's not -- better switch to backed up
    directory.

    *) http://foldingforum.org/viewtopic.php?f=55&t=17774

    
    CAUTION: The Kraken may produce sub-optimal results
             with nodes with no memory installed.



8. Support

    Using AMDZone forum is recommended. Please post in this thread:

    http://www.amdzone.com/phpbb3/viewtopic.php?f=521&t=138463

