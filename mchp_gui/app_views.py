import tkinter as tk
from collections import deque
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg

HISTORY_LEN = 500


class AppView:
    """Base class for application-specific display views."""

    def __init__(self, parent):
        self.frame = tk.Frame(parent)
        self.frame.pack(fill=tk.BOTH, expand=True)

    def update(self, values):
        raise NotImplementedError

    def destroy(self):
        self.frame.destroy()


class IMUView(AppView):
    LABELS = ["ax", "ay", "az"]
    COLORS = ["#e74c3c", "#2ecc71", "#3498db"]

    def __init__(self, parent):
        super().__init__(parent)

        self.num_frame = tk.Frame(self.frame)
        self.num_frame.pack(fill=tk.X, padx=10, pady=5)

        self.num_vars = []
        for i, label in enumerate(self.LABELS):
            tk.Label(self.num_frame, text=f"{label}:", font=("Consolas", 14)).grid(
                row=0, column=i * 2, padx=(10, 2)
            )
            var = tk.StringVar(value="0.00")
            tk.Label(
                self.num_frame,
                textvariable=var,
                font=("Consolas", 14, "bold"),
                fg=self.COLORS[i],
                width=8,
                anchor="e",
            ).grid(row=0, column=i * 2 + 1, padx=(0, 10))
            self.num_vars.append(var)

        self.history = [deque(maxlen=HISTORY_LEN) for _ in self.LABELS]

        self.fig = Figure(figsize=(6, 3), dpi=100)
        self.fig.subplots_adjust(left=0.1, right=0.95, top=0.95, bottom=0.12)
        self.ax = self.fig.add_subplot(111)
        self.ax.set_ylabel("m/s²")
        self.lines = []
        for i, label in enumerate(self.LABELS):
            (line,) = self.ax.plot([], [], color=self.COLORS[i], label=label, linewidth=1)
            self.lines.append(line)
        self.ax.legend(loc="upper right", fontsize=8)
        self.ax.set_xlim(0, HISTORY_LEN)
        self.ax.set_ylim(-12, 12)

        self.canvas = FigureCanvasTkAgg(self.fig, master=self.frame)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

    def update(self, values):
        for i in range(min(len(values), 3)):
            self.num_vars[i].set(f"{values[i]:8.2f}")
            self.history[i].append(values[i])
            self.lines[i].set_data(range(len(self.history[i])), list(self.history[i]))
        self.ax.set_xlim(0, max(HISTORY_LEN, len(self.history[0])))
        self.canvas.draw_idle()


class HRMView(AppView):
    COLOR_BPM = "#e74c3c"
    COLOR_PPG = "#3498db"

    def __init__(self, parent):
        super().__init__(parent)

        self.bpm_var = tk.StringVar(value="-- BPM")
        tk.Label(
            self.frame,
            textvariable=self.bpm_var,
            font=("Consolas", 36, "bold"),
            fg=self.COLOR_BPM,
        ).pack(pady=10)

        self.history = deque(maxlen=HISTORY_LEN)

        self.fig = Figure(figsize=(6, 3), dpi=100)
        self.fig.subplots_adjust(left=0.1, right=0.95, top=0.95, bottom=0.12)
        self.ax = self.fig.add_subplot(111)
        self.ax.set_ylabel("PPG")
        (self.line,) = self.ax.plot([], [], color=self.COLOR_PPG, linewidth=1)
        self.ax.set_xlim(0, HISTORY_LEN)
        self.ax.set_ylim(-500, 500)

        self.canvas = FigureCanvasTkAgg(self.fig, master=self.frame)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

    def update(self, values):
        bpm = int(values[0]) if len(values) > 0 else 0
        ppg = values[1] if len(values) > 1 else 0.0

        self.bpm_var.set(f"{bpm} BPM" if bpm > 0 else "-- BPM")
        self.history.append(ppg)
        self.line.set_data(range(len(self.history)), list(self.history))
        self.ax.set_xlim(0, max(HISTORY_LEN, len(self.history)))
        ydata = list(self.history)
        if ydata:
            ymin, ymax = min(ydata), max(ydata)
            margin = max(abs(ymax - ymin) * 0.1, 10)
            self.ax.set_ylim(ymin - margin, ymax + margin)
        self.canvas.draw_idle()


class RangeView(AppView):
    COLOR = "#e67e22"

    def __init__(self, parent):
        super().__init__(parent)

        self.dist_var = tk.StringVar(value="-- mm")
        tk.Label(
            self.frame,
            textvariable=self.dist_var,
            font=("Consolas", 36, "bold"),
            fg=self.COLOR,
        ).pack(pady=10)

        self.history = deque(maxlen=HISTORY_LEN)

        self.fig = Figure(figsize=(6, 3), dpi=100)
        self.fig.subplots_adjust(left=0.1, right=0.95, top=0.95, bottom=0.12)
        self.ax = self.fig.add_subplot(111)
        self.ax.set_ylabel("mm")
        (self.line,) = self.ax.plot([], [], color=self.COLOR, linewidth=1)
        self.ax.set_xlim(0, HISTORY_LEN)
        self.ax.set_ylim(0, 250)

        self.canvas = FigureCanvasTkAgg(self.fig, master=self.frame)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

    def update(self, values):
        dist = values[0] if values else 0.0
        self.dist_var.set(f"{dist:.0f} mm")
        self.history.append(dist)
        self.line.set_data(range(len(self.history)), list(self.history))
        self.ax.set_xlim(0, max(HISTORY_LEN, len(self.history)))
        self.canvas.draw_idle()


class WeatherView(AppView):
    LABELS = ["Temp (°C)", "Press (kPa)", "Humidity (%)"]
    COLORS = ["#e74c3c", "#3498db", "#2ecc71"]

    def __init__(self, parent):
        super().__init__(parent)

        self.num_frame = tk.Frame(self.frame)
        self.num_frame.pack(fill=tk.X, padx=10, pady=5)

        self.num_vars = []
        for i, label in enumerate(self.LABELS):
            tk.Label(self.num_frame, text=label, font=("Consolas", 12)).grid(
                row=0, column=i * 2, padx=(10, 2)
            )
            var = tk.StringVar(value="--")
            tk.Label(
                self.num_frame,
                textvariable=var,
                font=("Consolas", 14, "bold"),
                fg=self.COLORS[i],
                width=8,
                anchor="e",
            ).grid(row=0, column=i * 2 + 1, padx=(0, 10))
            self.num_vars.append(var)

        self.history = [deque(maxlen=HISTORY_LEN) for _ in self.LABELS]

        self.fig = Figure(figsize=(6, 3), dpi=100)
        self.fig.subplots_adjust(left=0.1, right=0.95, top=0.95, bottom=0.12)
        self.ax = self.fig.add_subplot(111)
        self.lines = []
        for i, label in enumerate(self.LABELS):
            (line,) = self.ax.plot([], [], color=self.COLORS[i], label=label, linewidth=1)
            self.lines.append(line)
        self.ax.legend(loc="upper right", fontsize=8)
        self.ax.set_xlim(0, HISTORY_LEN)

        self.canvas = FigureCanvasTkAgg(self.fig, master=self.frame)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

    def update(self, values):
        fmts = ["{:.1f}", "{:.1f}", "{:.1f}"]
        for i in range(min(len(values), 3)):
            self.num_vars[i].set(fmts[i].format(values[i]))
            self.history[i].append(values[i])
            self.lines[i].set_data(range(len(self.history[i])), list(self.history[i]))
        self.ax.set_xlim(0, max(HISTORY_LEN, len(self.history[0]) if self.history[0] else 1))
        self.ax.relim()
        self.ax.autoscale_view(scaley=True, scalex=False)
        self.canvas.draw_idle()


VIEW_MAP = {
    "IMU": ("6-DOF IMU", IMUView),
    "HRM": ("Heart Rate Monitor", HRMView),
    "RNG": ("Range Sensor", RangeView),
    "WX": ("Weather Station", WeatherView),
}
