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
                        if (x[2] <= 10301.189453125) {
                            if (x[2] <= 8268.736572265625) {
                                return 3;
                            }

                            else {
                                if (x[2] <= 8828.22802734375) {
                                    return 0;
                                }

                                else {
                                    if (x[1] <= 478759.453125) {
                                        return 3;
                                    }

                                    else {
                                        return 0;
                                    }
                                }
                            }
                        }

                        else {
                            if (x[2] <= 31295.5478515625) {
                                if (x[1] <= 555243.53125) {
                                    return 1;
                                }

                                else {
                                    if (x[2] <= 29667.6328125) {
                                        if (x[1] <= 595159.5) {
                                            if (x[1] <= 572996.3125) {
                                                return 2;
                                            }

                                            else {
                                                return 2;
                                            }
                                        }

                                        else {
                                            if (x[1] <= 596201.65625) {
                                                return 1;
                                            }

                                            else {
                                                return 2;
                                            }
                                        }
                                    }

                                    else {
                                        if (x[1] <= 572237.4375) {
                                            return 1;
                                        }

                                        else {
                                            return 2;
                                        }
                                    }
                                }
                            }

                            else {
                                if (x[2] <= 32226.4501953125) {
                                    if (x[2] <= 31693.7666015625) {
                                        return 1;
                                    }

                                    else {
                                        return 2;
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