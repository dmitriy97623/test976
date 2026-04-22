# src/gui/pin_view.py
import tkinter as tk
from tkinter import ttk


class PinView(ttk.Frame):
    def __init__(self, parent):
        super().__init__(parent)
        self.pwm_pins = {"D3", "D5", "D6", "D9", "D10", "D11"}
        self.used_pins = {"A0", "D0", "D1"}  # по умолчанию
        self.labels = {}
        self.create_pins()

    def create_pins(self):
        analog_pins = ["A0", "A1", "A2", "A3", "A4", "A5"]
        digital_pins = [f"D{i}" for i in range(14)]

        # Аналоговые
        ttk.Label(self, text="Аналоговые:", font=("Arial", 10, "bold")).grid(row=0, column=0, sticky="w", pady=(0, 5))
        for i, pin in enumerate(analog_pins):
            label = tk.Label(self, text=f"{pin} ●", font=("Courier", 10), fg="red")
            label.grid(row=1, column=i * 2, columnspan=2, padx=5)
            self.labels[pin] = label

        # Цифровые
        ttk.Label(self, text="Цифровые:", font=("Arial", 10, "bold")).grid(row=2, column=0, sticky="w", pady=(10, 5))
        for i, pin in enumerate(digital_pins):
            label = tk.Label(self, text=f"{pin} ●", font=("Courier", 10), fg="red")
            label.grid(row=3, column=i * 2, columnspan=2, padx=5)
            self.labels[pin] = label

    def update_pins(self, mode: str):
        used_pins = {"A0", "D0", "D1"}
        if "С дискретом" in mode or "калибровки" in mode:
            used_pins.add("D7")

        for pin, label in self.labels.items():
            if pin in used_pins:
                label.config(fg="green")
            elif pin in self.pwm_pins:
                label.config(fg="orange")
            else:
                label.config(fg="red")