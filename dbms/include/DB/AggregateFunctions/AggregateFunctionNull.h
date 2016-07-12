#pragma once

#include <DB/AggregateFunctions/IAggregateFunction.h>
#include <DB/Columns/ColumnNullable.h>
#include <DB/DataTypes/DataTypeNullable.h>

namespace DB
{

namespace ErrorCodes
{

extern const int LOGICAL_ERROR;

}

class AggregateFunctionNull : public IAggregateFunction
{
public:
	AggregateFunctionNull(AggregateFunctionPtr nested_function_holder_)
		: nested_function_holder{nested_function_holder_},
		nested_function{init(nested_function_holder)}
	{
	}

	String getName() const override
	{
		return nested_function.getName();
	}

	void setArguments(const DataTypes & arguments) override
	{
		argument_count = arguments.size();
		is_nullable.reserve(arguments.size());

		for (const auto & arg : arguments)
		{
			bool res = arg.get()->isNullable();
			is_nullable.push_back(res);
			if (res)
				has_nullable_columns = true;
		}

		if (has_nullable_columns)
		{
			DataTypes new_args;
			new_args.reserve(arguments.size());

			for (const auto & arg : arguments)
			{
				if (arg.get()->isNullable())
				{
					const DataTypeNullable & nullable_type = static_cast<const DataTypeNullable &>(*(arg.get()));
					const DataTypePtr & nested_type = nullable_type.getNestedType();
					new_args.push_back(nested_type);
				}
				else
					new_args.push_back(arg);
			}

			nested_function.setArguments(new_args);
		}
		else
			nested_function.setArguments(arguments);
	}

	void setParameters(const Array & params)
	{
		nested_function.setParameters(params);
	}

	DataTypePtr getReturnType() const override
	{
		return nested_function.getReturnType();
	}

	void create(AggregateDataPtr place) const override
	{
		nested_function.create(place);
	}

	void destroy(AggregateDataPtr place) const noexcept override
	{
		nested_function.destroy(place);
	}

	bool hasTrivialDestructor() const override
	{
		return nested_function.hasTrivialDestructor();
	}

	size_t sizeOfData() const override
	{
		return nested_function.sizeOfData();
	}

	size_t alignOfData() const override
	{
		return nested_function.alignOfData();
	}

	void add(AggregateDataPtr place, const IColumn ** columns, size_t row_num) const override
	{
		auto init = [&]() -> std::unique_ptr<std::vector<const IColumn *> >
		{
			std::unique_ptr<std::vector<const IColumn *> > res;

			if (!has_nullable_columns)
				return nullptr;

			return std::move(std::make_unique<std::vector<const IColumn *> >(argument_count));
		};

		/// This container stores the columns we really pass to the nested function.
		/// We use thread local storage in order to minimize allocations since add()
		/// may be called millions of times by a few threads.
		thread_local std::unique_ptr<std::vector<const IColumn *> > passed_columns_holder = init();

		if (!has_nullable_columns)
			nested_function.add(place, columns, row_num);
		else
		{
			std::vector<const IColumn *> & passed_columns = *passed_columns_holder;

			for (size_t i = 0; i < argument_count; ++i)
			{
				if (is_nullable[i])
				{
					const ColumnNullable & nullable_col = static_cast<const ColumnNullable &>(*columns[i]);
					if (nullable_col.isNullAt(row_num))
					{
						/// If at least one column has a null value in the current row,
						/// we don't process this row.
						return;
					}
					passed_columns[i] = nullable_col.getNestedColumn().get();
				}
				else
				{
					passed_columns[i] = columns[i];
				}
			}

			nested_function.add(place, passed_columns.data(), row_num);
		}
	}

	void merge(AggregateDataPtr place, ConstAggregateDataPtr rhs) const override
	{
		nested_function.merge(place, rhs);
	}

	void serialize(ConstAggregateDataPtr place, WriteBuffer & buf) const override
	{
		nested_function.serialize(place, buf);
	}

	void deserialize(AggregateDataPtr place, ReadBuffer & buf) const override
	{
		nested_function.deserialize(place, buf);
	}

	void insertResultInto(ConstAggregateDataPtr place, IColumn & to) const override
	{
		nested_function.insertResultInto(place, to);
	}

	static void addFree(const IAggregateFunction * that, AggregateDataPtr place, const IColumn ** columns, size_t row_num)
	{
		return static_cast<const AggregateFunctionNull &>(*that).add(place, columns, row_num);
	}

	AddFunc getAddressOfAddFunction() const override
	{
		return &addFree;
	}

private:
	static IAggregateFunction & init(AggregateFunctionPtr & nested_function_holder_)
	{
		if (!nested_function_holder_)
			throw Exception{"Passed null pointer to aggregate function", ErrorCodes::LOGICAL_ERROR};
		return *nested_function_holder_.get();
	}

private:
	AggregateFunctionPtr nested_function_holder;
	IAggregateFunction & nested_function;
	std::vector<bool> is_nullable;
	size_t argument_count = 0;
	bool has_nullable_columns = false;
};

}