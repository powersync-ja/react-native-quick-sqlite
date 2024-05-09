export interface BaseObserverInterface<T extends BaseListener> {
  registerListener(listener: Partial<T>): () => void;
}

export type BaseListener = {
  [key: string]: (...event: any) => any;
};

export class BaseObserver<T extends BaseListener = BaseListener>
  implements BaseObserverInterface<T>
{
  protected listeners: Set<Partial<T>>;

  constructor() {
    this.listeners = new Set();
  }

  registerListener(listener: Partial<T>): () => void {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }

  iterateListeners(cb: (listener: Partial<T>) => any) {
    for (const listener of this.listeners) {
      cb(listener);
    }
  }
}
