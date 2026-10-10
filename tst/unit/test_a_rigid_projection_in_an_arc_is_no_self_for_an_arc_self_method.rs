// sqlx-core's `PoolInner::connect`: `connect_options.connect()` on an
// `Arc<<DB::Connection as Connection>::Options>` calls the options' trait
// method. `PoolInner`'s own `fn connect(self: &Arc<Self>)` cannot apply -
// upstream assembles an inherent impl only for an autoderef step of its own
// type, and the rigid projection inside the `Arc` is a type of its own - so
// the call is not taken for a recursive call of the method it is written in.
use std::sync::Arc;

pub trait Database {
    type Connection: Connection;
}

pub trait Connection {
    type Options: ConnectOptions;
}

pub trait ConnectOptions {
    fn connect(&self) -> u32;
}

pub struct PoolInner<DB: Database>(DB);

impl<DB: Database> PoolInner<DB> {
    pub fn connect(self: &Arc<Self>) -> &'static str {
        "pool"
    }

    pub fn options(o: Arc<<DB::Connection as Connection>::Options>) -> u32 {
        let r = o.connect();
        r + 1
    }
}

pub struct Pg;
pub struct PgConnection;
pub struct PgOptions(u32);

impl Database for Pg {
    type Connection = PgConnection;
}

impl Connection for PgConnection {
    type Options = PgOptions;
}

impl ConnectOptions for PgOptions {
    fn connect(&self) -> u32 {
        self.0
    }
}

fn main() {
    assert_eq!(PoolInner::<Pg>::options(Arc::new(PgOptions(6))), 7);
    assert_eq!(Arc::new(PoolInner(Pg)).connect(), "pool");
}
