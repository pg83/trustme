//@ compile-only
//@ crate-type: lib

pub enum Message {
    Value { byte: u8 },
}

pub union Storage {
    pub byte: u8,
}

pub fn enum_loop() -> Message {
    Message::Value { byte: loop {} }
}

pub fn union_loop() -> Storage {
    Storage { byte: loop {} }
}
