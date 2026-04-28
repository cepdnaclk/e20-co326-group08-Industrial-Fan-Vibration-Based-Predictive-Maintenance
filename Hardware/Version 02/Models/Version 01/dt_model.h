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
                        if (x[2] <= 9791.23486328125) {
                            if (x[2] <= 8543.9267578125) {
                                return 2;
                            }

                            else {
                                return 3;
                            }
                        }

                        else {
                            if (x[2] <= 19432.6611328125) {
                                if (x[2] <= 16971.6044921875) {
                                    if (x[1] <= 527401.28125) {
                                        return 1;
                                    }

                                    else {
                                        return 0;
                                    }
                                }

                                else {
                                    if (x[1] <= 519322.828125) {
                                        return 1;
                                    }

                                    else {
                                        if (x[1] <= 530804.65625) {
                                            if (x[1] <= 523228.921875) {
                                                return 0;
                                            }

                                            else {
                                                return 0;
                                            }
                                        }

                                        else {
                                            if (x[1] <= 531152.53125) {
                                                return 1;
                                            }

                                            else {
                                                return 0;
                                            }
                                        }
                                    }
                                }
                            }

                            else {
                                return 1;
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
                            case 4:
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