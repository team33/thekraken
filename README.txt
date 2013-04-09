

Contents:
0. License
1. What does The Kraken do?
2. How is The Kraken useful?
3. Supported configurations (PLEASE READ!)
4. Upgrade recommendations
4.1. Upgrade recommendations: from The Kraken 0.2
4.2. Upgrade recommendations: from The Kraken 0.3
4.3. Upgrading from The Kraken 0.4, The Kraken 0.6-pre4 or The Kraken 0.6
5. Building and installing The Kraken
5.1. Pre-requisites
5.2. Building and installation
6. Wrapping
6.1. Wrapping: V6 client
6.2. Wrapping: V7 client
6.3. Dynamic Load Balancing
7. Unwrapping
8. How do I know it's working?
9. Known issues and caveats
10. Support
11. Credits



0. License

  The Kraken is licensed under GNU General Public License version 2.
  See COPYING.txt for details.



1. What does The Kraken do?

  The Kraken wraps around FahCore binaries and sets CPU affinity as soon
  as subsequent worker threads get created.

  It also creates synthetic load with the intent of triggering DLB (Dynamic
  Load Balancing) which improves performance in majority of configurations.



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



4. Upgrade recommendations

4.1. Upgrade recommendations: from The Kraken 0.2

    It is recommended to unwrap FahCores and remove (or archive) The Kraken
    from the system for the sake of housekeeping.

    1. Go to client directory (shutting the client down is not required)
    2. Run './thekraken-0.2 -u'; it should give output similar to the following:

    $ ./thekraken-0.2 -u
    thekraken: The Kraken 0.2
    thekraken: Processor affinity wrapper for Folding@Home
    thekraken: The Kraken comes with ABSOLUTELY NO WARRANTY; licensed under GPLv2
    thekraken: performing uninstallation
    thekraken: FahCore_a5.exe: wrapper succesfully uninstalled
    thekraken: FahCore_a3.exe: wrapper succesfully uninstalled
    thekraken: finished uninstallation
    $ 

    3. Run 'rm -f thekraken-0.2*'
    4. Follow instructions in 6.1 or 6.2, depending on configuration.



4.2. Upgrade recommendations: from The Kraken 0.3

    It is recommended to unwrap FahCores and remove (or archive) The Kraken
    from the system for the sake of housekeeping.

    If you downloaded and installed The Kraken 0.3 (which wasn't very
    fortunate release), there are two recommended action items --
    unwrapping FahCores in client directory and removing The Kraken
    from /usr/local/bin.

    1. Run (as root) '/usr/local/bin/thekraken -yu /var/lib/fahclient';
       on Ubuntu prepend the line with 'sudo'.
    2. Run (as root) 'rm -f /usr/local/bin/thekraken'; on Ubuntu
       prepend the line with 'sudo'.
    3. Follow instructions in 6.1 or 6.2, depending on configuration.



4.3. Upgrading from The Kraken 0.4, The Kraken 0.6-pre4 or The Kraken 0.6

    1. Build and install The Kraken per section 5.
    2. Go to client directory (shutting the client down is not required)
    3. Run 'thekraken -u'; it should give output similar to the following:


       $ thekraken -u
       thekraken: The Kraken 0.7-pre11 (compiled Sun May 20 19:36:47 MDT 2012 by fah@tentacle)
       thekraken: Processor affinity wrapper for Folding@Home
       thekraken: The Kraken comes with ABSOLUTELY NO WARRANTY; licensed under GPLv2
       thekraken: performing uninstallation from .
       thekraken: /home/fah/fah-6.34/FahCore_a3.exe: wrapper succesfully uninstalled
       thekraken: /home/fah/fah-6.34/FahCore_a5.exe: wrapper succesfully uninstalled
       thekraken: removing configuration file
       thekraken: finished uninstallation, 2 out of 2 file(s) processed
       $ 


    4. Run 'thekraken -w'; it should give output similar to the following:

       $ thekraken -i
       thekraken: The Kraken 0.7-pre11 (compiled Sun May 20 19:36:47 MDT 2012 by fah@tentacle)
       thekraken: Processor affinity wrapper for Folding@Home
       thekraken: The Kraken comes with ABSOLUTELY NO WARRANTY; licensed under GPLv2
       thekraken: performing installation to .
       thekraken: /home/fah/fah-6.34/FahCore_a3.exe: wrapper succesfully installed
       thekraken: /home/fah/fah-6.34/FahCore_a5.exe: wrapper succesfully installed
       thekraken: finished installation, 2 out of 2 files processed
       $ 



5. Building and installing The Kraken

5.1. Pre-requisites

    1. Make sure that (where appropriate)
       - NUMA is enabled,
       - node interleave is disabled,
       - ACPI SRAT is enabled

    2. Make sure that make and gcc are installed; on Ubuntu issue

       sudo apt-get install gcc make

    3. Make sure that all target FahCores have been downloaded
       by the client.



5.2. Building and installation

    1. Run 'make'
    2. Run 'make install' as root (on Ubuntu, run 'sudo make install')
    
    Voila. The Kraken is available in /usr/bin.



6. Wrapping

6.1. Wrapping: V6 client

    1. Stop the client
    2. Go to client directory
    3. Run 'thekraken -w'

       Doing so should result in something along the following:

       $ thekraken -i
       thekraken: The Kraken 0.7-pre11 (compiled Sun May 20 19:36:47 MDT 2012 by fah@tentacle)
       thekraken: Processor affinity wrapper for Folding@Home
       thekraken: The Kraken comes with ABSOLUTELY NO WARRANTY; licensed under GPLv2
       thekraken: performing installation to .
       thekraken: /home/fah/fah-6.34/FahCore_a3.exe: wrapper succesfully installed
       thekraken: /home/fah/fah-6.34/FahCore_a5.exe: wrapper succesfully installed
       thekraken: finished installation, 2 out of 2 files processed
       $ 

    4. Re-start the client.



6.2. Wrapping: V7 client

    As single V7 client installation may be run off more than one "home"
    directory, wrapping process is little more complicated.
    It is imperative client's home directory is determined prior to
    wrapping.

    1. Make sure the client is running (and folding)
    2. Determine client's "home" directory; run (as root; on Ubuntu prepend
       with 'sudo'):

       stat /proc/$(ps auxw | awk '/FahCo[^[]/ { print $2 ; exit }' )/exe | head -1 | cut -f 3 -d \`  | sed s=cores.*\$=cores/=

       It should return path, such as: '/var/lib/fahclient/cores/'.
       Make note of this path.

    3. Stop the client
    4. Run (as root; on Ubuntu prepend with 'sudo'):

       thekraken -w path-determined-in-step-2

       Doing so should result in something along the following:

       $ thekraken -i /var/lib/fahclient/cores/
       thekraken: The Kraken 0.4 (compiled Tue Apr 12 20:27:47 MDT 2011 by fah@tentacle)
       thekraken: Processor affinity wrapper for Folding@Home
       thekraken: The Kraken comes with ABSOLUTELY NO WARRANTY; licensed under GPLv2
       thekraken: performing installation to /var/lib/fahclient/cores/
       thekraken: descend into /var/lib/fahclient/cores/www.stanford.edu and all other subdirectories [Y/n]?

       If path appears correct, confirm with 'y' and press Enter. This should result
       in something along the following:

       thekraken: /var/lib/fahclient/cores/www.stanford.edu/~pande/Linux/AMD64/Core_a3.fah/FahCore_a3: wrapper succesfully installed
       thekraken: /var/lib/fahclient/cores/www.stanford.edu/~pande/Linux/AMD64/Core_a5.fah/FahCore_a5: wrapper succesfully installed
       thekraken: finished installation, 2 out of 2 files processed
       $ 

    5. Start the client



6.3. Dynamic Load Balancing

    Background: GROMACS employs Dynamic Load Balancing (DLB)
    feature that aims at improving performance.

    GROMACS configuration used by FahCores enables DLB the moment
    cumulative performance loss due to load imbalance exceeds 5%.

    When enabled, DLB reduces times of bigadv units by noticable
    amount of time. Reports include reduction of 30s with P6903
    and 45 seconds with P6904 (sometimes more).

    Depending on WU and system configuration (or even system state),
    DLB gets enabled in a way that may appear random (sometimes it's
    several minutes into WU; at other times it may be as late
    as 90% into WU, sometimes it doesn't engage at all).

    The Kraken 0.7 features a novel way of making FahCores enable
    DLB. It creates synthetic load on every other CPU; said load comes
    and goes (with certain duty cycle) until DLB is engaged or until
    deadline is reached (5 minutes by default).

    DLB triggering is enabled by default. To disable it, add '-c dlbload=0'
    parameter to the command line, when wrapping, e.g.
    'thekraken -w -c dlbload=0'.
    If already wrapped: unwrap, then re-wrap with '-c dlbload=0'.
    Stopping the client is not required.



7. Unwrapping

    Follow wrapping instructions but replace 'thekraken -w' with 'thekraken -u'.



8. How do I know it's working?

    Method #1:

        While folding, run 'top'. Look for process consuming most CPU. 'COMMAND'
        of that process should read 'thekraken-FahCo', e.g.:

           PID  USER     PR  NI  VIRT  RES  SHR S   %CPU %MEM    TIME+   P COMMAND
          8411  fah      39  19 6452m 4.5g 3188 S 4801.6 14.3  52424:56 24 thekraken-FahCo


    Method #2 (examination of FahCores' affinities):

        While folding, run:
   
          for i in $(ps -L auxw | awk '/FahCo[^[]/ { if ($4 > 10) print $3 }' | sort -n -k1,1) ; do taskset -pc $i  ; done

        Doing so should give you sequential processor numbers starting with 0 in its
        output, e.g.:

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

   
   
9. Know issues and caveats

    CAUTION: FahCore_a5 is known to be problematic at user-induced shutdowns*.
             To be on a safe side make a backup of complete client directory
             before hitting Ctrl+C!

    To tell whether checkpoint was written correctly check the size
    of work/wudata_XX.ckp file (XX being current slot number).
    It should be 75160 (for core A5). If it's not -- better switch to backed up
    directory.

    *) http://foldingforum.org/viewtopic.php?f=55&t=17774

    
    CAUTION: The Kraken may produce sub-optimal results
             when nodes with no memory installed are present.



10. Support

    Using AMDZone forum is recommended. Please post in this thread:

    http://www.amdzone.com/phpbb3/viewtopic.php?f=521&t=138463



11. Credits

    The Kraken was written and is maintained by Kris Rusocki <kszysiu@gmail.com>
    Synthload DLB research and coding was done by Stephen Gordon <firedfly@gmail.com>

    Special thanks go to:

        brutis at AMDZone -- excellent V7 test feedback

        sfield -- original DLB enagement observation

        musky at [H]ardForum -- alternative method for installation
                                confirmation

        firedfly at [H]ardForum -- help with troubleshooting
                                   client-on-remote-filesystem-with-off-clock
                                   issue
