use std::any::Any;

fn payload_as_str(payload: &dyn Any) -> &str {
    if let Some(value) = payload.downcast_ref::<String>() {
        value.as_str()
    } else {
        "not a string"
    }
}

fn main() {
    let value = String::from("value");
    assert_eq!(payload_as_str(&value), "value");
}
