import { ReactNode, useEffect } from "react";
import { createPortal } from "react-dom";

interface ModalProps {
  title: string;
  children: ReactNode;
  footer?: ReactNode;
  onClose: () => void;
  width?: number | string;
}

export default function Modal({ title, children, footer, onClose, width = 520 }: ModalProps): JSX.Element {
  useEffect(() => {
    const handleKey = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        onClose();
      }
    };
    window.addEventListener("keydown", handleKey);
    return () => window.removeEventListener("keydown", handleKey);
  }, [onClose]);

  return createPortal(
    <div className="modal-overlay" role="dialog" aria-modal="true" aria-label={title}>
      <div className="modal" style={{ maxWidth: width }}>
        <header className="modal__header">
          <h2>{title}</h2>
          <button type="button" className="button tertiary" onClick={onClose}>
            Close
          </button>
        </header>
        <div className="modal__body">{children}</div>
        {footer && <footer className="modal__footer">{footer}</footer>}
      </div>
    </div>,
    document.body
  );
}
