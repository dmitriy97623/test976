#!/usr/bin/env python3
"""
Точка входа в приложение
"""
from tkinter import Tk
from src.simulator_gui import ArduinoSimulator

if __name__ == "__main__":
    root = Tk()
    app = ArduinoSimulator(root)
    root.mainloop()