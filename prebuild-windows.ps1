prebuild -t 14.0.0;
prebuild -t 14.0.0 -a ia32;
prebuild -t 15.0.0;
prebuild -t 15.0.0 -a ia32;
prebuild -t 16.0.0;
prebuild -t 16.0.0 -a ia32;
prebuild -t 17.0.0;
prebuild -t 17.0.0 -a ia32;
prebuild -t 18.0.0;

prebuild -r electron -t 23.1.0 -t 22.3.0 -t 21.4.1 -t 20.3.12 -t 19.1.9 -t 18.3.15 -t 17.4.11 -t 16.2.8 -t 15.5.7
prebuild -r electron -a ia32 -t 23.1.0 -t 22.3.0 -t 21.4.1 -t 20.3.12 -t 19.1.9 -t 18.3.15 -t 17.4.11 -t 16.2.8 -t 15.5.7