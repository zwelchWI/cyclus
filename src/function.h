#ifndef CYCLUS_CYCLOPTS_FUNCTION_H_
#define CYCLUS_CYCLOPTS_FUNCTION_H_

#include <map>
#include <utility>
#include <string>

#include <boost/shared_ptr.hpp>

#include "variable.h"

//#include "solver_interface.h"

namespace cyclus {

/// function base class
class Function {
 public:
  /// constructor
  Function();

  /// virtual destructor for base class
  virtual ~Function() {};

  /// get a modifier
  /// @param v the variable being modified
  double GetModifier(Variable::Ptr v);

  /// @return the beginning iterator to the function variable
  std::map<Variable::Ptr, double>::const_iterator begin();

  /// @return the ending iterator to the function variable
  std::map<Variable::Ptr, double>::const_iterator end();

  /// add a variable to the constraint
  /// @param v a pointer to the variable to add
  /// @param modifier the modifier for that variable in the function
  void AddVariable(Variable::Ptr v, double modifier);

  /// @return number of variables in the function
  int NumVars();

  /// print the function
  virtual std::string Print();

 private:
  /// a container of all variables and their corresponding constant
  std::map<Variable::Ptr, double> constituents_;    
};

/// derived class for constraints
class Constraint : public Function {
 public:
  typedef boost::shared_ptr<Constraint> Ptr;

  /// the possible equality relations
  enum EqualityRelation {EQ, GT, GTEQ, LT, LTEQ};
  
  /// constructor
  /// @param eq_r the equality relation, e.g., <=, =, >, etc.
  /// @param rhs the value of the right hand side
  Constraint(EqualityRelation eq_r, double rhs);

  /// @return the equality relation
  EqualityRelation eq_relation();

  /// @return the rhs
  double rhs();

  /// print the constraint
  virtual std::string Print();

 private:
  /// the type of equality relation
  EqualityRelation eq_relation_;

  /// the rhs value
  double rhs_;

  /// turn eq_relation_ into a string
  std::string EqRToStr();
};
  
/// derived class for objective functions
class ObjectiveFunction : public Function {
 public: 
  typedef boost::shared_ptr<ObjectiveFunction> Ptr;

  /// the possible direction
  enum Direction {MIN, MAX};

  /// constructor
  /// @param dir the direction of optimization (max or min)
  explicit ObjectiveFunction(Direction dir);

  /// @return the Direction
  Direction dir();

  /// print the objective function
  virtual std::string Print();

 private:
  /// the Direction
  Direction dir_;

  /// turn dir_ into a string
  std::string DirToStr();
};

} // namespace cyclus

#endif
