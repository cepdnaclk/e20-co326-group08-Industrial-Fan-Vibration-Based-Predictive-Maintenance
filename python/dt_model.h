#pragma once
#include <cstdarg>
namespace Eloquent {
    namespace ML {
        namespace Port {
            class DecisionTree {
                public:
                    /**
                    * Predict class for features vector
                    */
                    int predict(float *x) {
                        if (x[1] <= 33025.986328125) {
                            if (x[2] <= 1132.6725463867188) {
                                return 3;
                            }

                            else {
                                if (x[2] <= 15825.27001953125) {
                                    return 0;
                                }

                                else {
                                    return 3;
                                }
                            }
                        }

                        else {
                            if (x[2] <= 32490.365234375) {
                                if (x[0] <= 119.140625) {
                                    if (x[2] <= 26114.767578125) {
                                        return 1;
                                    }

                                    else {
                                        if (x[2] <= 26349.1455078125) {
                                            return 2;
                                        }

                                        else {
                                            if (x[0] <= 31.25) {
                                                return 1;
                                            }

                                            else {
                                                return 1;
                                            }
                                        }
                                    }
                                }

                                else {
                                    if (x[1] <= 93556.4921875) {
                                        return 1;
                                    }

                                    else {
                                        if (x[0] <= 123.046875) {
                                            return 2;
                                        }

                                        else {
                                            if (x[2] <= 30544.5341796875) {
                                                return 2;
                                            }

                                            else {
                                                return 1;
                                            }
                                        }
                                    }
                                }
                            }

                            else {
                                if (x[2] <= 36734.623046875) {
                                    if (x[1] <= 165092.390625) {
                                        return 1;
                                    }

                                    else {
                                        if (x[0] <= 91.796875) {
                                            return 1;
                                        }

                                        else {
                                            return 2;
                                        }
                                    }
                                }

                                else {
                                    return 1;
                                }
                            }
                        }
                    }

                    /**
                    * Predict readable class name
                    */
                    const char* predictLabel(float *x) {
                        return idxToLabel(predict(x));
                    }

                    /**
                    * Convert class idx to readable name
                    */
                    const char* idxToLabel(uint8_t classIdx) {
                        switch (classIdx) {
                            case 0:
                            return "normal";
                            case 1:
                            return "blocked";
                            case 2:
                            return "unbalanced";
                            case 3:
                            return "off";
                            default:
                            return "Houston we have a problem";
                        }
                    }

                protected:
                };
            }
        }
    }