extern crate forward_closure_rpit_metadata_producer as producer;

pub fn consume() -> u8 {
    let result = producer::make(7_u8);
    (result.0.callback)()
}
