// Extracted from library/core/src/net/ip_addr.rs:191
#![allow(unused)]
#![feature(ip)]
fn main() {
    
    use std::net::Ipv6Addr;
    use std::net::Ipv6MulticastScope::*;
    
    // An IPv6 multicast address with global scope (`ff0e::`).
    let address = Ipv6Addr::new(0xff0e, 0, 0, 0, 0, 0, 0, 0);
    
    // Will print "Global scope".
    match address.multicast_scope() {
        Some(InterfaceLocal) => println!("Interface-Local scope"),
        Some(LinkLocal) => println!("Link-Local scope"),
        Some(RealmLocal) => println!("Realm-Local scope"),
        Some(AdminLocal) => println!("Admin-Local scope"),
        Some(SiteLocal) => println!("Site-Local scope"),
        Some(OrganizationLocal) => println!("Organization-Local scope"),
        Some(Global) => println!("Global scope"),
        Some(_) => println!("Unknown scope"),
        None => println!("Not a multicast address!")
    }
}
