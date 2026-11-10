#![feature(no_core, lang_items)]
#![no_core]
#![no_main]

#[lang = "pointee_sized"]
trait PointeeSized {}
#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}
#[lang = "sized"]
trait Sized: MetaSized {}
#[lang = "copy"]
trait Copy {}
impl Copy for u16 {}
impl Copy for i32 {}

trait FutureValue {
    type Output;
}

trait IntoFuture {
    type Output;
    type IntoFuture: FutureValue<Output = Self::Output>;

    #[lang = "into_future"]
    fn into_future(self) -> Self::IntoFuture;
}

struct Ready(u16);

impl FutureValue for Ready {
    type Output = u16;
}

struct Ticket;

impl IntoFuture for Ticket {
    type Output = u16;
    type IntoFuture = Ready;

    fn into_future(self) -> Ready {
        Ready(29)
    }
}

#[no_mangle]
extern "C" fn main() -> i32 {
    let ready = Ticket.into_future();
    match ready.0 {
        29 => 0,
        _ => 1,
    }
}
