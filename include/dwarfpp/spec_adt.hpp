#ifndef __DWARFPP_SPEC_ADT_HPP
#define __DWARFPP_SPEC_ADT_HPP

#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <boost/shared_ptr.hpp>
#include <boost/iterator_adaptors.hpp>
#include <boost/iterator/filter_iterator.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/make_shared.hpp>
#include <boost/enable_shared_from_this.hpp>
#include "lib.hpp"
#include "expr.hpp"
#include "attr.hpp"

#define NULL_SHARED_PTR(type) boost::shared_ptr<type>()

namespace dwarf
{
    // -- FIXME bring abstract_dieset into spec?
	namespace spec 
    {
	    using namespace lib;
        class compile_unit_die;
        class program_element_die;
        struct basic_die;
        struct file_toplevel_die;
        std::ostream& operator<<(std::ostream& s, const basic_die& d);
        
		class abstract_dieset : public boost::enable_shared_from_this<abstract_dieset>
        {
        public:
            /* This is all you need to denote a member of a dieset. */
            struct position
            {
            	abstract_dieset *p_ds;
                Dwarf_Off off;
				bool operator==(const position& arg) const 
				{ return this->p_ds == arg.p_ds && this->off == arg.off; }
                void canonicalize_position()
                { 
/*                	try // test whether we're pointing at a real DIE
                    {
                    	if (!p_ds || off == std::numeric_limits<Dwarf_Off>::max())
                        {
                        	throw No_entry();
                        }
                        // FIXME: this will NOT throw No_entry! UNdefined behaviour! 
                    	//p_ds->operator[](off);
                        assert(p_ds->find(off) != p_ds->end());
                    } 
                    catch (No_entry) // if not, set us to be the end sentinel
                    {
                    	this->off = std::numeric_limits<Dwarf_Off>::max();
                    }*/ // FIXME: why is this function necessary?
                }
            };
            
            struct order_policy
            {
            	virtual int increment(position& pos, 
                	std::deque<position>& path) = 0;
                virtual int decrement(position& pos,
                	std::deque<position>& path) = 0;
			protected:
				const int behaviour; // used for equality comparison
				
				enum
				{
					 NO_BEHAVIOUR,
					 PREDICATED_BEHAVIOUR,
					 DEPTHFIRST_BEHAVIOUR, // FIXME: do I actually use the predicates in these?
					 BREADTHFIRST_BEHAVIOUR,
					 SIBLINGS_BEHAVIOUR
				};
				
				order_policy(int behaviour) : behaviour(behaviour) {}
			public:
				
				/* Equality of policies: 
				 * By default, policies are tested only for behavioural equality.
				 * Subtypes might want to narrow that to
				 * - all policies of the same class are equal
				 * or
				 * - policies must exhibit value-equality.
				 *
				 * Virtual dispatch asymmetry here:
				 * in the worst case this will get dispatched
				 * to a base class. But that only happens if
				 * the two policies are not of the same class.
				 * We don't want such to be equal anyway, so
				 * this is okay. 
				 */ 
				order_policy() : behaviour(0) {}
				
				virtual bool half_equal(const order_policy& arg) const
				{
					// this is the "most generous" equality relation
					return this->behaviour == arg.behaviour;
				}
				bool operator==(const order_policy& arg) const
				{
					return this->half_equal(arg) && arg.half_equal(*this);
				}
			};
            
            // FIXME: remember what this is for....
            struct die_pred : public std::unary_function<spec::basic_die, bool>
            {
				virtual bool operator()(const spec::basic_die& d) const = 0;
			};
            
            struct policy : order_policy, die_pred 
			{
			protected:
				policy(int behaviour) : order_policy(behaviour) {}
			public:
				policy() : order_policy(PREDICATED_BEHAVIOUR) {}
			};
            /* Default policy: depth-first order, all match. */
            struct default_policy : policy
            {
            	// depth-first iteration
            	int increment(position& pos,
                	std::deque<position>& path);
                int decrement(position& pos,
                	std::deque<position>& path);
			protected:
				default_policy(int behaviour) : policy(behaviour) {}
			public:
				default_policy() : policy(DEPTHFIRST_BEHAVIOUR) {}
				
                // always true
                bool operator()(const spec::basic_die& d) const { return true; }
			};
            static default_policy default_policy_sg;
            /* Breadth-first policy: breadth-first order, all match. */
            struct bfs_policy : policy
            {
            	// a queue of paths
            	std::deque<std::deque<position> > m_queue;
            	// breadth-first iteration
            	int increment(position& pos,
                	std::deque<position>& path);
                int decrement(position& pos,
                	std::deque<position>& path);
			protected:
				bfs_policy(int behaviour) : policy(behaviour), m_queue() {}
			public:
            	bfs_policy() : policy(BREADTHFIRST_BEHAVIOUR), m_queue() {}
				
				bool half_equal(const order_policy& arg) const
				{
					return dynamic_cast<const bfs_policy*>(&arg)
						&& dynamic_cast<const bfs_policy*>(&arg)->m_queue == this->m_queue;
				}
                
                // always true
                bool operator()(const spec::basic_die& d) const { return true; }
			};
            //static bfs_policy bfs_policy_sg;
            // we *don't* create a bfs policy singleton because each BFS traversal
            // has to keep its own state (queue of nodes)
            /* Sibling policy: for children iterators */
            struct siblings_policy : policy 
            {
            	int increment(position& pos,
                	std::deque<position>& path);
                int decrement(position& pos,
                	std::deque<position>& path);
			protected:
				siblings_policy(int behaviour) : policy(behaviour) {}
			public:
				siblings_policy() : policy(SIBLINGS_BEHAVIOUR) {}
                
                // always true
                bool operator()(const spec::basic_die& d) const { return true; }
            };
            static siblings_policy siblings_policy_sg;
            
            struct basic_iterator_base 
            : public position
            {
            	typedef std::pair<Dwarf_Off, boost::shared_ptr<spec::basic_die> > pair_type;
                std::deque<position> path_from_root;
            	policy& m_policy;
                bool operator==(const basic_iterator_base& arg) const
                { return this->off == arg.off && this->p_ds == arg.p_ds
                	&& (off == std::numeric_limits<Dwarf_Off>::max () || // HACK: == end() works
                    	this->m_policy == arg.m_policy);               // for any policy
                }
                bool operator!=(const basic_iterator_base& arg) const { return !(*this == arg); }
                basic_iterator_base(abstract_dieset& ds, Dwarf_Off off,
                	 const std::deque<position>& path_from_root,
                	 policy& pol = default_policy_sg);
                basic_iterator_base() // path_from_root is empty
                : position({0, 0UL}), m_policy(default_policy_sg) { canonicalize_position(); } 
                typedef std::bidirectional_iterator_tag iterator_category;
                typedef spec::basic_die value_type;
                typedef Dwarf_Off difference_type;
                typedef spec::basic_die *pointer;
                typedef spec::basic_die& reference;
				
				basic_iterator_base& operator=(const basic_iterator_base& arg)
				{
					assert(m_policy == arg.m_policy);
					this->path_from_root = arg.path_from_root;
					return *this;
				}
            };

            struct iterator;
        	virtual iterator find(Dwarf_Off off) = 0;
            virtual iterator begin() = 0;
            virtual iterator end() = 0;
            
            virtual std::deque< position >
            path_from_root(Dwarf_Off off) = 0;
            
            virtual boost::shared_ptr<spec::basic_die> operator[](Dwarf_Off off) const = 0;
            boost::shared_ptr<spec::basic_die> operator[](Dwarf_Off off)
            { return const_cast<const abstract_dieset *>(this)->operator[](off); }
           
            struct iterator
            : public boost::iterator_adaptor<iterator, // Derived
                    basic_iterator_base,        // Base
                    boost::shared_ptr<spec::basic_die>, // Value
                    boost::use_default, // Traversal
                    boost::shared_ptr<spec::basic_die> // Reference
                > 
            {
                typedef boost::shared_ptr<spec::basic_die> Value;
            	typedef basic_iterator_base Base;
            	
                iterator() : iterator::iterator_adaptor_() {}
            	
                iterator(Base p) : iterator::iterator_adaptor_(p) {}
            	
                iterator(abstract_dieset& ds, Dwarf_Off off, 
                	const std::deque<position>& path_from_root, 
                    policy& pol = default_policy_sg)  
                : iterator::iterator_adaptor_(
                	basic_iterator_base(ds, off, path_from_root, pol)) {}

                iterator(const position& pos, 
                	const std::deque<position>& path_from_root,
                    policy& pol = default_policy_sg)  
                : iterator::iterator_adaptor_(basic_iterator_base(
                	*pos.p_ds, pos.off, path_from_root, pol)) {}
                
                // copy-like constructor that changes policy
                iterator(const iterator& arg, policy& pol) : 
                	iterator::iterator_adaptor_(basic_iterator_base(
                    	*arg.base().p_ds, arg.base().off, arg.base().path_from_root, pol)) {}
                            	
                void increment()        
                { this->base().m_policy.increment(this->base_reference(), this->base_reference().path_from_root); }
            	void decrement()        
                { this->base().m_policy.decrement(this->base_reference(), this->base_reference().path_from_root); }
                Value dereference() { return this->base().p_ds->operator[](this->base().off); }
                Value dereference() const { return this->base().p_ds->operator[](this->base().off); }
                position& pos() { return this->base_reference(); }
                const std::deque<position>& path() { return this->base_reference().path_from_root; }
            };
            virtual boost::shared_ptr<spec::file_toplevel_die> toplevel() = 0; /* NOT const */
            virtual const spec::abstract_def& get_spec() const = 0;
        };        
        
	    struct basic_die
        {
        	friend std::ostream& operator<<(std::ostream& s, const basic_die& d);
        
		    virtual Dwarf_Off get_offset() const = 0;
            virtual Dwarf_Half get_tag() const = 0;
            
            virtual boost::shared_ptr<basic_die> get_parent() = 0;
			//boost::shared_ptr<basic_die> get_parent()
			//{ return this->get_ds().move_up(get_this()); }
			boost::shared_ptr<basic_die> get_parent() const
            { return const_cast<basic_die *>(this)->get_parent(); }           
            
            virtual boost::shared_ptr<basic_die> get_first_child() = 0;
			//boost::shared_ptr<basic_die> get_first_child() 
			//{ return this->get_ds().move_down(get_this()); }
            boost::shared_ptr<basic_die> get_first_child() const
            { return const_cast<basic_die *>(this)->get_first_child(); }
            virtual Dwarf_Off get_first_child_offset() const = 0;
			//Dwarf_Off get_first_child_offset() const
			//{ return this->get_ds().move_down(get_this()); }
            
			boost::optional<Dwarf_Off> first_child_offset() const
            { 	try { return this->get_first_child_offset(); } 
            	catch (No_entry) { return boost::optional<Dwarf_Off>(); } }
            
            virtual boost::shared_ptr<basic_die> get_next_sibling() = 0;
            //boost::shared_ptr<basic_die> get_next_sibling()
			//{ return this->get_ds().move_right(get_this()); }
			boost::shared_ptr<basic_die> get_next_sibling() const
            { return const_cast<basic_die *>(this)->get_next_sibling(); }
            virtual Dwarf_Off get_next_sibling_offset() const = 0;
			//Dwarf_Off get_next_sibling_offset() const
			//{ return this->get_ds().move_right(get_this()); }
            
			boost::optional<Dwarf_Off> next_sibling_offset() const
            { 	try { return this->get_next_sibling_offset(); } 
            	catch (No_entry) { return boost::optional<Dwarf_Off>(); } }
            
            virtual boost::optional<std::string> get_name() const = 0;
            virtual const spec::abstract_def& get_spec() const = 0;

            virtual const abstract_dieset& get_ds() const
            { return const_cast<basic_die *>(this)->get_ds(); }
            virtual abstract_dieset& get_ds() = 0;
        protected: /* TENTATIVE: we don't want get_ds() used by end users, because
                    * there is no canonical dieset.... */
            //virtual const abstract_dieset& get_ds() const
            //{ return const_cast<basic_die *>(this)->get_ds(); }
            //virtual abstract_dieset& get_ds() = 0;
			
			// so how do we implement this in the stacking-dieset sense?
			// we could have an explicit delegation pointer stored here
			// but better just to use overriding if we can -- how?
			// subclass, new constructors passing stacking_dieset reference,
			// override get_ds()... but THEN have to wrap everything s.t. uses the new get_ds
			// -- better to set up ds dynamically
			// but then still, requires canonical dieset
			// that's okay -- master/slave separation
			// when we add a dieset to the stacking dieset,
			// we update its master dieset pointer
			// -- in encap, each die stores an m_ds
			// -- in lib, each die similarly stores a p_ds
			// BUT another problem:
			// encap::dies are computed eagerly
			// so will already be storing the wrong m_ds
			// (and it's a reference so you can't change it)
			
			// It seems like there should be a name for this problem. 
			// By including backlinks (a.k.a. contextual knowledge)
			// in our data structure, we've made it
			// brittle, because 
			// changes to that contextual knowledge now require
			// changes to many places in the source code.
		public:

            // recover a shared_ptr to this DIE, from a plain this ptr
            boost::shared_ptr<basic_die> get_this();
            boost::shared_ptr<basic_die> get_this() const;

			abstract_dieset::iterator 
            iterator_here(abstract_dieset::policy& pol = abstract_dieset::default_policy_sg)
            { return abstract_dieset::iterator(
            	this->get_ds(),
                this->get_offset(),
                this->get_ds().find(this->get_offset()).base().path_from_root, 
                pol); }

            // FIXME: iterator pair //virtual std::pair< > get_children() = 0;
            
            abstract_dieset::iterator children_begin() 
            { 
                if (first_child_offset()) 
                {
                    return this->get_first_child()->iterator_here(
                        abstract_dieset::siblings_policy_sg);
                }
                else
                {
                	return this->get_ds().end();
                }
            }
            abstract_dieset::iterator children_end() { return this->get_ds().end(); }

        protected: // public interface is to downcast
            virtual std::map<Dwarf_Half, encap::attribute_value> get_attrs() = 0;
            // not a const function because may create backrefs -----------^^^
		public:
            boost::optional<std::vector<std::string> >
            ident_path_from_root() const;

            boost::optional<std::vector<std::string> >
            ident_path_from_cu() const;

			/* These, and other resolve()-style functions, are not const 
             * because they need to be able to return references to mutable
             * found DIEs. FIXME: provide const overloads. */
            boost::shared_ptr<basic_die> 
            nearest_enclosing(Dwarf_Half tag);

            boost::shared_ptr<compile_unit_die> 
            enclosing_compile_unit();

            boost::shared_ptr<basic_die>
            find_sibling_ancestor_of(boost::shared_ptr<basic_die> d);
        };

        struct with_runtime_location_die : public virtual basic_die
        {
        	struct sym_binding_t 
            { 
            	Dwarf_Off file_relative_start_addr; 
                Dwarf_Unsigned size;
            };
        	virtual encap::loclist get_runtime_location() const;
            virtual boost::optional<Dwarf_Off> contains_addr(
            	Dwarf_Addr file_relative_addr,
                sym_binding_t (*sym_resolve)(const std::string& sym, void *arg) = 0, 
                void *arg = 0) const;
            /*virtual std::vector<std::pair<Dwarf_Addr, Dwarf_Addr> >
			file_relative_extents(
                sym_binding_t (*sym_resolve)(const std::string& sym, void *arg) = 0, 
                void *arg = 0) const;*/
		};
        
        struct with_stack_location_die : public virtual basic_die
        {
			virtual boost::optional<Dwarf_Off> contains_addr(
					Dwarf_Addr absolute_addr,
					Dwarf_Signed frame_base_addr,
					Dwarf_Off dieset_relative_ip,
					dwarf::lib::regs *p_regs = 0) const;
			virtual boost::optional<encap::loclist> get_location() const = 0;
			virtual boost::optional<boost::shared_ptr<spec::type_die> > get_type() const = 0;
			/* virtual Dwarf_Addr calculate_addr(
				Dwarf_Signed frame_base_addr,
				Dwarf_Off dieset_relative_ip,
				dwarf::lib::regs *p_regs = 0) const;*/
		};
                
	    struct with_named_children_die : public virtual basic_die
        {
            virtual 
            boost::shared_ptr<basic_die>
            named_child(const std::string& name);

            template <typename Iter>
            boost::shared_ptr<basic_die> 
            resolve(Iter path_pos, Iter path_end);

            boost::shared_ptr<basic_die> 
            resolve(const std::string& name);

            template <typename Iter>
            boost::shared_ptr<basic_die> 
            scoped_resolve(Iter path_pos, Iter path_end);

            template <typename Iter>
            void
            scoped_resolve_all(Iter path_pos, Iter path_end, 
            	std::vector<boost::shared_ptr<basic_die> >& results, int max = 0) 
            {
            	boost::shared_ptr<basic_die> found_from_here = resolve(path_pos, path_end);
            	if (found_from_here) 
                { 
                	results.push_back(found_from_here); 
                    if (max != 0 && results.size() == max) return;
                }
                if (this->get_tag() == 0) return;
                else // find our nearest encloser that has named children
                {
                	boost::shared_ptr<spec::basic_die> p_encl = this->get_parent();
                    while (boost::dynamic_pointer_cast<with_named_children_die>(p_encl) == 0)
                    {
                    	if (p_encl->get_tag() == 0) return;
                    	p_encl = p_encl->get_parent();
                    }
                    // we've found an encl that has named children
                    dynamic_cast<with_named_children_die&>(*p_encl)
                    	.scoped_resolve_all(path_pos, path_end, results);
                    return;
                }
			}            

            boost::shared_ptr<basic_die> 
            scoped_resolve(const std::string& name);
        };
        
        class abstract_mutable_dieset : public abstract_dieset
        {
        public:
        	virtual 
            boost::shared_ptr<spec::basic_die> 
            insert(Dwarf_Off key, boost::shared_ptr<spec::basic_die> val) = 0;
        };

        
/****************************************************************/
/* begin generated ADT includes                                 */
/****************************************************************/
#define forward_decl(t) class t ## _die;
#define declare_base(base) virtual base ## _die
#define base_fragment(base) base ## _die(ds, p_d) {}
#define initialize_base(fragment) fragment ## _die(ds, p_d)
#define constructor(fragment, ...) 
        
#define begin_class(fragment, base_inits, ...) \
	struct fragment ## _die : __VA_ARGS__ { \
    	constructor(fragment, base_inits)
#define base_initializations(...) __VA_ARGS__
#define end_class(fragment) \
	};

#define stored_type_string std::string
#define stored_type_flag bool
#define stored_type_unsigned Dwarf_Unsigned
#define stored_type_signed Dwarf_Signed
#define stored_type_offset Dwarf_Off
#define stored_type_half Dwarf_Half
#define stored_type_ref Dwarf_Off
#define stored_type_tag Dwarf_Half
#define stored_type_loclist dwarf::encap::loclist
#define stored_type_address dwarf::encap::attribute_value::address
#define stored_type_refdie boost::shared_ptr<spec::basic_die> 
#define stored_type_refdie_is_type boost::shared_ptr<spec::type_die> 
#define stored_type_rangelist dwarf::encap::rangelist

#define attr_optional(name, stored_t) \
	virtual boost::optional<stored_type_ ## stored_t> get_ ## name() const = 0; \
  	boost::optional<stored_type_ ## stored_t> name() const { return get_ ## name(); }

#define super_attr_optional(name, stored_t)

#define attr_mandatory(name, stored_t) \
	virtual stored_type_ ## stored_t get_ ## name() const = 0; \
  	stored_type_ ## stored_t name() const { return get_ ## name(); } 

#define super_attr_mandatory(name, stored_t)

template<Dwarf_Half Tag>
struct has_tag : public std::unary_function<boost::shared_ptr<spec::basic_die>, bool>
{
	bool operator()(const boost::shared_ptr<spec::basic_die> arg) const
    { 
    	//std::cerr << "testing whether die at " << std::hex << arg->get_offset() << std::dec 
        //	<< " has tag " << Tag << std::endl;
    	return arg->get_tag() == Tag; 
    }
};
//typedef std::unary_function<boost::shared_ptr<spec::basic_die>, boost::shared_ptr<spec:: arg ## _die > > type_of_dynamic_pointer_cast;
#define child_tag(arg) \
	typedef boost::shared_ptr<spec:: arg ## _die >(*type_of_dynamic_pointer_cast_ ## arg)(boost::shared_ptr<spec::basic_die> const&); \
	typedef boost::filter_iterator<has_tag< DW_TAG_ ## arg >, spec::abstract_dieset::iterator> arg ## _basic_iterator; \
	typedef boost::transform_iterator<type_of_dynamic_pointer_cast_ ## arg, arg ## _basic_iterator> arg ## _iterator; \
    arg ## _iterator arg ## _children_begin() { return arg ## _iterator(arg ## _basic_iterator(children_begin(), children_end()), boost::dynamic_pointer_cast<spec:: arg ## _die> ); } \
    arg ## _iterator arg ## _children_end() { return arg ## _iterator(arg ## _basic_iterator(children_end(), children_end()), boost::dynamic_pointer_cast<spec:: arg ## _die> ); }

        struct file_toplevel_die : public virtual with_named_children_die
        {
            struct is_visible
            {
                bool operator()(boost::shared_ptr<spec::basic_die> p) const;
            };

            template <typename Iter>
            boost::shared_ptr<basic_die>
            visible_resolve(Iter path_pos, Iter path_end);
            
            virtual boost::shared_ptr<basic_die>
            visible_named_child(const std::string& name);
            
            child_tag(compile_unit)
        };
                
// define additional virtual dies first -- note that
// some virtual DIEs are defined manually (above)
begin_class(program_element, base_initializations(initialize_base(basic)), declare_base(basic))
        attr_optional(decl_file, unsigned)
        attr_optional(decl_line, unsigned)
        attr_optional(decl_column, unsigned)
        attr_optional(prototyped, flag)
        attr_optional(declaration, flag)
        attr_optional(external, flag)
        attr_optional(visibility, unsigned)
end_class(program_element)
begin_class(type, base_initializations(initialize_base(program_element)), declare_base(program_element))
        attr_optional(byte_size, unsigned)
        virtual boost::optional<Dwarf_Unsigned> calculate_byte_size() const;
        virtual bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
		virtual boost::shared_ptr<type_die> get_concrete_type() const;
        boost::shared_ptr<type_die> get_concrete_type();
end_class(type)
begin_class(type_chain, base_initializations(initialize_base(type)), declare_base(type))
        attr_optional(type, refdie_is_type)
        boost::optional<Dwarf_Unsigned> calculate_byte_size() const;
        boost::shared_ptr<type_die> get_concrete_type() const;
end_class(type_chain)

#define extra_decls_compile_unit \
		boost::optional<Dwarf_Unsigned> implicit_array_base() const; 
#define extra_decls_subprogram \
        boost::optional< std::pair<Dwarf_Off, boost::shared_ptr<spec::with_stack_location_die> > > \
        contains_addr_as_frame_local_or_argument( \
            	    Dwarf_Addr absolute_addr, \
                    Dwarf_Off dieset_relative_ip, \
                    Dwarf_Signed *out_frame_base, \
                    dwarf::lib::regs *p_regs = 0) const; \
        bool is_variadic() const;
#define extra_decls_variable \
        bool has_static_storage() const;
#define extra_decls_array_type \
		boost::optional<Dwarf_Unsigned> element_count() const; \
        boost::optional<Dwarf_Unsigned> calculate_byte_size() const; \
        bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_pointer_type \
		boost::shared_ptr<type_die> get_concrete_type() const; \
        boost::optional<Dwarf_Unsigned> calculate_byte_size() const; \
        bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_reference_type \
		boost::shared_ptr<type_die> get_concrete_type() const; \
        boost::optional<Dwarf_Unsigned> calculate_byte_size() const; \
        bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_base_type \
		bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_structure_type \
		bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_union_type \
		bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_class_type \
		bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_enumeration_type \
		bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_subroutine_type \
		bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_member \
		boost::optional<Dwarf_Unsigned> byte_offset_in_enclosing_type() const;
#define extra_decls_inheritance \
		boost::optional<Dwarf_Unsigned> byte_offset_in_enclosing_type() const;

#include "dwarf3-adt.h"

#undef extra_decls_inheritance
#undef extra_decls_member
#undef extra_decls_subroutine_type
#undef extra_decls_enumeration_type
#undef extra_decls_class_type
#undef extra_decls_union_type
#undef extra_decls_structure_type
#undef extra_decls_base_type
#undef extra_decls_reference_type
#undef extra_decls_pointer_type
#undef extra_decls_array_type
#undef extra_decls_variable
#undef extra_decls_compile_unit

#undef forward_decl
#undef declare_base
#undef base_fragment
#undef initialize_base
#undef constructor
#undef begin_class
#undef base_initializations
#undef end_class
#undef stored_type_string
#undef stored_type_flag
#undef stored_type_unsigned
#undef stored_type_signed
#undef stored_type_offset
#undef stored_type_half
#undef stored_type_ref
#undef stored_type_tag
#undef stored_type_loclist
#undef stored_type_address
#undef stored_type_refdie
#undef stored_type_refdie_is_type
#undef stored_type_rangelist
#undef attr_optional
#undef attr_mandatory
#undef super_attr_optional
#undef super_attr_mandatory
#undef child_tag

/****************************************************************/
/* end generated ADT includes                                   */
/****************************************************************/

        template <typename Iter>
        boost::shared_ptr<basic_die> 
        with_named_children_die::resolve(Iter path_pos, Iter path_end)
        {
            /* We can't return "this" because it's not a shared_ptr, and we don't
             * want to create a duplicate count by creating a new shared ptr from it.
             * So we ask our containing dieset to make a new one for us. In the case
             * of lib::die, this will create an entirely new Die. In the case of
             * encap::die, it will just retrieve the stored shared_ptr and use that. */
            this->get_offset();
            this->get_ds();
            if (path_pos == path_end) return this->get_ds().operator[](this->get_offset());
            Iter cur_plus_one = path_pos; cur_plus_one++;
            if (cur_plus_one == path_end) return named_child(*path_pos);
            else
            {
                auto found = named_child(*path_pos);
                if (!found) return NULL_SHARED_PTR(basic_die);
                auto p_next_hop =
                    boost::dynamic_pointer_cast<with_named_children_die>(found);
                if (!p_next_hop) return NULL_SHARED_PTR(basic_die);
                else return p_next_hop->resolve(++path_pos, path_end);
            }
        }
        
        template <typename Iter>
        boost::shared_ptr<basic_die> 
        with_named_children_die::scoped_resolve(Iter path_pos, Iter path_end)
        {
            if (resolve(path_pos, path_end)) return this->get_ds().operator[](this->get_offset());
            if (this->get_tag() == 0) return NULL_SHARED_PTR(basic_die);
            else // find our nearest encloser that has named children
            {
                boost::shared_ptr<spec::basic_die> p_encl = this->get_parent();
                while (boost::dynamic_pointer_cast<with_named_children_die>(p_encl) == 0)
                {
                    if (p_encl->get_tag() == 0) return NULL_SHARED_PTR(basic_die);
                    p_encl = p_encl->get_parent();
                }
                // we've found an encl that has named children
                return boost::dynamic_pointer_cast<with_named_children_die>(p_encl)
                    ->scoped_resolve(path_pos, path_end);
            }
		}
		
        template <typename Iter>
        boost::shared_ptr<basic_die>
        file_toplevel_die::visible_resolve(Iter path_pos, Iter path_end)
        {
            is_visible visible;
            boost::shared_ptr<basic_die> found;
            for (auto i_cu = this->compile_unit_children_begin();
                    i_cu != this->compile_unit_children_end(); i_cu++)
            {
                if (path_pos == path_end) { found = this->get_this(); break; }
                auto found_under_cu = (*i_cu)->named_child(*path_pos);

                Iter cur_plus_one = path_pos; cur_plus_one++;
                if (cur_plus_one == path_end && found_under_cu
                        && visible(found_under_cu))
                { found = found_under_cu; break; }
                else
                {
                    if (!found_under_cu || 
                            !visible(found_under_cu)) continue;
                    auto p_next_hop =
                        boost::dynamic_pointer_cast<with_named_children_die>(found_under_cu);
                    if (!p_next_hop) continue; // try next compile unit
                    else 
                    { 
                        auto found_recursive = p_next_hop->resolve(++path_pos, path_end);
                        if (found_recursive) { found = found_recursive; break; }
                        // else continue
                    }
                }
            }
            if (found) return found; else return boost::shared_ptr<basic_die>();
        }
    }
}

#endif
