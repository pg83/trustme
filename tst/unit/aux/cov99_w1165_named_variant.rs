pub enum Message {
    Data { count: u32 },
}

pub use self::Message::Data as Payload;
