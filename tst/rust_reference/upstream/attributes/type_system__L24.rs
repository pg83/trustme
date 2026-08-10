// Extracted from src/attributes/type_system.md:24
#![allow(unused)]
fn main() {
    #[non_exhaustive]
    pub struct Config {
        pub window_width: u16,
        pub window_height: u16,
    }
    
    #[non_exhaustive]
    pub struct Token;
    
    #[non_exhaustive]
    pub struct Id(pub u64);
    
    #[non_exhaustive]
    pub enum Error {
        Message(String),
        Other,
    }
    
    pub enum Message {
        #[non_exhaustive] Send { from: u32, to: u32, contents: String },
        #[non_exhaustive] Reaction(u32),
        #[non_exhaustive] Quit,
    }
    
    // Non-exhaustive structs can be constructed as normal within the defining crate.
    let config = Config { window_width: 640, window_height: 480 };
    let token = Token;
    let id = Id(4);
    
    // Non-exhaustive structs can be matched on exhaustively within the defining crate.
    let Config { window_width, window_height } = config;
    let Token = token;
    let Id(id_number) = id;
    
    let error = Error::Other;
    let message = Message::Reaction(3);
    
    // Non-exhaustive enums can be matched on exhaustively within the defining crate.
    match error {
        Error::Message(ref s) => { },
        Error::Other => { },
    }
    
    match message {
        // Non-exhaustive variants can be matched on exhaustively within the defining crate.
        Message::Send { from, to, contents } => { },
        Message::Reaction(id) => { },
        Message::Quit => { },
    }
}
