#![feature(ip)]

use std::net::IpAddr;
use std::str::FromStr;

fn main() {
    macro_rules! ip {
        ($s:expr) => {
            IpAddr::from_str($s).unwrap()
        };
    }

    macro_rules! check {
        ($s:expr) => {
            check!($s, 0);
        };

        ($s:expr, $mask:expr) => {{
            let unspec: u8 = 1 << 0;
            let loopback: u8 = 1 << 1;
            let global: u8 = 1 << 2;
            let multicast: u8 = 1 << 3;
            let doc: u8 = 1 << 4;
            let benchmarking: u8 = 1 << 5;

            if ($mask & unspec) == unspec {
                assert!(ip!($s).is_unspecified());
            } else {
                assert!(!ip!($s).is_unspecified());
            }
            if ($mask & loopback) == loopback {
                assert!(ip!($s).is_loopback());
            } else {
                assert!(!ip!($s).is_loopback());
            }
            if ($mask & global) == global {
                assert!(ip!($s).is_global());
            } else {
                assert!(!ip!($s).is_global());
            }
            if ($mask & multicast) == multicast {
                assert!(ip!($s).is_multicast());
            } else {
                assert!(!ip!($s).is_multicast());
            }
            if ($mask & doc) == doc {
                assert!(ip!($s).is_documentation());
            } else {
                assert!(!ip!($s).is_documentation());
            }
            if ($mask & benchmarking) == benchmarking {
                assert!(ip!($s).is_benchmarking());
            } else {
                assert!(!ip!($s).is_benchmarking());
            }
        }};
    }

    let unspec: u8 = 1 << 0;
    let loopback: u8 = 1 << 1;
    let global: u8 = 1 << 2;
    let multicast: u8 = 1 << 3;
    let doc: u8 = 1 << 4;
    let benchmarking: u8 = 1 << 5;

    check!("0.0.0.0", unspec);
    check!("0.0.0.1");
    check!("0.1.0.0");
    check!("10.9.8.7");
    check!("127.1.2.3", loopback);
    check!("172.31.254.253");
    check!("169.254.253.242");
    check!("192.0.2.183", doc);
    check!("192.1.2.183", global);
    check!("192.168.254.253");
    check!("198.51.100.0", doc);
    check!("203.0.113.0", doc);
    check!("203.2.113.0", global);
    check!("224.0.0.0", global | multicast);
    check!("239.255.255.255", global | multicast);
    check!("255.255.255.255");
    check!("198.18.0.0", benchmarking);
    check!("198.18.54.2", benchmarking);
    check!("198.19.255.255", benchmarking);
    check!("192.0.0.0");
    check!("192.0.0.255");
    check!("192.0.0.100");
    check!("240.0.0.0");
    check!("251.54.1.76");
    check!("254.255.255.255");
    check!("100.64.0.0");
    check!("100.127.255.255");
    check!("100.100.100.0");
    check!("::", unspec);
    check!("::1", loopback);
    check!("::2", global);
    check!("1::", global);
    check!("fc00::");
    check!("fdff:ffff::");
    check!("fe80:ffff::");
    check!("febf:ffff::");
    check!("fec0::", global);
    check!("ff01::", global | multicast);
    check!("ff02::", global | multicast);
    check!("ff03::", global | multicast);
    check!("ff04::", global | multicast);
    check!("ff05::", global | multicast);
    check!("ff08::", global | multicast);
    check!("ff0e::", global | multicast);
    check!("2001:db8:85a3::8a2e:370:7334", doc);
    check!("3fff:fff:ffff:ffff:ffff:ffff:ffff:ffff", doc);
    check!("2001:2::ac32:23ff:21", benchmarking);
    check!("102:304:506:708:90a:b0c:d0e:f10", global);
}
