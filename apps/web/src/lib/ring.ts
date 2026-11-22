// Fixed-capacity ring buffer. Memory stays bounded however long the dashboard
// stays open; the oldest item is overwritten first.

export class Ring<T> {
  private readonly items: (T | undefined)[];
  private start = 0;
  private count = 0;

  constructor(readonly capacity: number) {
    if (!Number.isInteger(capacity) || capacity < 1) throw new Error("capacity must be a positive integer");
    this.items = new Array<T | undefined>(capacity);
  }

  get size(): number {
    return this.count;
  }

  push(item: T): void {
    const index = (this.start + this.count) % this.capacity;
    this.items[index] = item;
    if (this.count < this.capacity) this.count += 1;
    else this.start = (this.start + 1) % this.capacity;
  }

  /** Oldest first. */
  toArray(): T[] {
    const out: T[] = [];
    for (let i = 0; i < this.count; i += 1) out.push(this.items[(this.start + i) % this.capacity] as T);
    return out;
  }

  last(): T | undefined {
    return this.count ? this.items[(this.start + this.count - 1) % this.capacity] : undefined;
  }

  clear(): void {
    this.items.fill(undefined);
    this.start = 0;
    this.count = 0;
  }
}
