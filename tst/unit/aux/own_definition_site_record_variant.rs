#![feature(decl_macro)]

pub enum Packet {
    Data { word: u16 },
    Empty,
}

use Packet::Data as Payload;

pub macro word($packet:expr) {
    match $packet {
        Payload { word: captured } => captured,
        _ => 0,
    }
}
