class Sensor_Emulator {
public:
	// constructor
	Sensor_Emulator(int i = 0) {
		total = i;
	}

	// interface to outside world
	void addNum(int number) {
		total += number;
	}

	// interface to outside world
	int getTotal() {
		return total;
	};

private:
	// hidden data from outside world
	int total;
};
