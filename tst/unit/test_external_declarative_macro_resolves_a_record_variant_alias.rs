//@ aux-build: own_definition_site_record_variant.rs

struct Payload { word: u16 }

fn main() {
    let local = Payload { word: 1000 };
    let packet = own_definition_site_record_variant::Packet::Data { word: 43 };
    assert_eq!(own_definition_site_record_variant::word!(packet), 43);
    assert_eq!(own_definition_site_record_variant::word!(
        own_definition_site_record_variant::Packet::Empty), 0);
    assert_eq!(local.word, 1000);
}
